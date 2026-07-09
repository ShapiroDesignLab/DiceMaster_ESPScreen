# ESPScreen Architecture

Firmware for one Adafruit Qualia ESP32-S3 board driving a 480×480 RGB666 LCD.
Six of these are embedded in the DiceMaster dice; each acts as an **SPI slave**
receiving media commands from the Raspberry Pi (`DiceMaster_Central`) and
rendering them.

Source of truth is the code in `Dicemaster/`. This document explains how the
pieces fit so you don't have to reverse-engineer it from the headers.

Related docs:
- `../README.md` — hardware, libraries, repository layout
- `docs/runbooks/flashing.md` — how to flash a board
- `docs/runbooks/local-dev.md` — compile/flash/debug loop and serial logs
- Root repo `docs/protocol.md` — the wire protocol, shared with Central

---

## System modes

Set `current_mode` in `Dicemaster.ino` before flashing:

| Mode | Behavior |
|---|---|
| `PRODUCTION` | SPI slave; waits for and renders commands from the Pi. This is the shipping mode. |
| `DEMO` | Cycles through built-in animations and text demos. No SPI needed — good for a standalone bench check. |
| `TESTING` | Runs `TestSuite` (`tests.h`) at startup, then idles. |
| `SPI_DEBUG` | Prints SPI/decode statistics every ~3 seconds. Use when diagnosing transport issues. |

---

## Core pipeline (PRODUCTION mode)

```
SPI Slave HW ──▶ decode_task (FreeRTOS) ──▶ DecodingHandler ──▶ Screen queue ──▶ screen->update() @ 60Hz
                 (task notification)         (protocol parse)     (enqueue)        (main loop render)
```

1. **`SPIDriver`** (`spi.h`) — owns a pool of 16 DMA buffers (8 KB each,
   `SPI_BUFFER_SIZE`/`BUFFER_POOL_SIZE`). Configured as an SPI slave in
   `SPI_MODE0` on the default HSPI pins (`slave.begin()` with no override). On
   each completed transaction the hardware notifies the decode task
   (`ulTaskNotifyTakeIndexed`) — event-driven, no polling. Buffers flow through
   the pipeline zero-copy and are re-queued immediately after processing.

2. **`DecodingHandler`** (`decoding_handler.h`) — parses raw SPI buffers using
   the binary protocol, reassembles multi-chunk image transfers, and on
   completion calls `Screen::enqueue()`. It returns each buffer to the
   `SPIDriver` via the buffer-return callback when done.

3. **`Screen`** (`screen.h` / `screen.cpp`) — owns a FreeRTOS queue of up to 32
   `MediaContainer*`. `screen->update()`, called from the main loop at ~60 Hz,
   dequeues and renders the next ready item.

Because the transport is event-driven and buffers are recycled immediately,
there is no separate requeue task and no continuous poll loop — see the summary
comment at the bottom of `spi.h`.

---

## Wire protocol (`protocol.h` + `constants.h`)

Binary SPI protocol with a 5-byte header:

```
[ SOF_MARKER = 0x7E ][ MessageType ][ ScreenID bitmask ][ Length (2B) ]
```

**Screen-ID filtering (CAN-bus style):** the `SCREEN_ID` global (set in
`Dicemaster.ino`, default 6) is used as a *bit position*. A message is accepted
only if bit `SCREEN_ID` is set in the header's ScreenID byte. This lets the Pi
broadcast one message to any subset of the six boards.

**Message types:** `TEXT_BATCH`, `IMAGE_TRANSFER_START`, `IMAGE_CHUNK`,
`IMAGE_TRANSFER_END`, `PING_REQUEST` / `PING_RESPONSE`, `ACK`, `ERROR`,
`BACKLIGHT_ON` / `BACKLIGHT_OFF`.

**Image transfer:** `IMAGE_TRANSFER_START` (carries chunk 0) → `IMAGE_CHUNK` × N
→ optionally `IMAGE_TRANSFER_END`. A single-chunk image lives entirely in the
START message.

> The protocol is **shared with `DiceMaster_Central`**. Both sides must agree on
> the header, message types, and chunking. The canonical spec is
> `docs/protocol.md` in the root repository; Central's encoder is
> `DiceMaster_Central/src/dicemaster_central/dicemaster_central/media_typing/protocol.py`.

---

## Media types (`media.h`)

All display content is a `MediaContainer` subclass in namespace `dice`:

| Type | Description |
|---|---|
| `TextGroup` | Collection of `Text` items sharing a background color and rotation |
| `Text` | One string with position, font, and color |
| `Image` | JPEG or RGB565 image; decodes asynchronously via a FreeRTOS task into PSRAM |

- **Colors** are RGB565 (16-bit). Named constants are prefixed `DICE_`
  (e.g. `DICE_BLACK`, `DICE_WHITE`) in `constants.h`.
- **Fonts** (`FontID`): `TF` (Latin/Unicode), `ARABIC`, `CHINESE`, `CYRILLIC`,
  `DEVANAGARI`.
- **Rotation** (`Rotation`): `ROT_0`, `ROT_90`, `ROT_180`, `ROT_270`. Text
  rotation maps 90°↔270° internally due to display orientation.

---

## Key configuration in `Dicemaster.ino`

```cpp
uint8_t SCREEN_ID = 6;                          // which bit this board listens on
Rotation DEFAULT_ROTATION = Rotation::ROT_0;    // default display orientation
SystemMode current_mode = SystemMode::PRODUCTION;
```

Each of the six boards must be flashed with a **distinct `SCREEN_ID`** matching
its position in the dice and the IDs Central addresses.

---

## Common code patterns

**Enqueue text** (ownership transfers to `Screen` on success):

```cpp
auto* group = new TextGroup(duration_ms, bg_color, font_color, rotation);
group->add_member(new Text("Hello", 0, FontID::TF, x, y));
if (!screen->enqueue(group)) delete group;   // delete if the queue was full
```

**Multi-chunk image:** create an `Image` with `num_chunks`, then call
`add_chunk_with_id()` for each chunk. The image auto-decodes once all chunks
arrive.

---

## Image asset pipeline (host side)

Images are embedded as C header files: `Dicemaster/bmp.hs/` (RGB565) and
`Dicemaster/jpg.hs/` (JPEG). Generate them from source images with the scripts
in `scripts/`:

```bash
# Crop + resize a single image to 480×480 JPEG
python3 scripts/img_processor.py test_images/foo.jpg

# Crop + resize a GIF/video to 240×240 JPEG frames
python3 scripts/img_processor.py test_images/animation.gif

# Convert an image to an RGB565 C header (→ bmp.hs/)
python3 scripts/img2rgb565.py test_images/foo.jpg

# Convert a JPEG to a C byte-array header (→ jpg.hs/)
python3 scripts/img2bytes.py test_images/foo.jpg
```

> `.hs` directories hold C headers with embedded binary assets — not Haskell.
