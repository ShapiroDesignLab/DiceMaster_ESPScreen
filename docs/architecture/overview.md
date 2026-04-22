# Firmware Architecture Overview

The DiceMaster_ESPScreen firmware runs on the Adafruit Qualia ESP32-S3 and drives a 480x480 RGB666 LCD display. Each board operates as an SPI slave, receiving display commands from a Raspberry Pi master over a binary protocol, then rendering text or JPEG images to the screen at 60 Hz.

## System Modes

The `current_mode` variable in `Dicemaster.ino` selects the operating mode at flash time:

- `PRODUCTION` — SPI slave mode; waits for commands from the Pi master.
- `DEMO` — cycles through built-in animations and text examples.
- `TESTING` — runs the `TestSuite` at startup, then idles.
- `SPI_DEBUG` — prints SPI and decode statistics to the serial monitor every 3 seconds.

## Full Data Flow (PRODUCTION mode)

```
Pi SPI master
    │
    │  Binary SPI packet (up to 8 KB per transaction)
    ▼
SPIDriver (dice_spi.h)
  - Pool of 16 DMA buffers, 8 KB each (ESP32DMASPI)
  - SPI transaction completes → hardware ISR notifies SPI_Decode FreeRTOS task
    │
    │  SPIBuffer* (zero-copy pointer)
    ▼
SPI_Decode task (decode_task_function)
  - Calls decoding_handler->enqueue_raw_buffer(buffer)
  - Wakes DecodingProcessor task via xTaskNotifyGive
    │
    │  SPIBuffer* in raw_buffer_queue (depth 32)
    ▼
DecodingHandler / DecodingProcessor task (decoding_handler.h)
  - Calls DProtocol::decode() → parses 5-byte header, routes by MessageType
  - TEXT_BATCH   → builds TextGroup + Text objects, returns immediately
  - IMAGE_START  → allocates Image, stores embedded chunk 0
  - IMAGE_CHUNK  → appends data; when all chunks received, Image is complete
  - VIDEO_*      → assembles H.264 NAL units, hands off to VideoStream
  - On completion → screen_ref->enqueue(MediaContainer*)
  - Returns SPIBuffer to SPI driver via buffer_return_callback → requeue
    │
    │  MediaContainer* in screen media queue (depth 32)
    ▼
Screen (screen.h / screen.cpp)
  - Main loop calls screen->update() every 16 ms (60 Hz)
  - Dequeues next ready MediaContainer
  - Dispatches to draw_img(), draw_textgroup(), or draw_bmp565_2x()
    │
    ▼
Display (480×480 RGB666 LCD via Arduino_GFX + esp_lcd_panel_rgb)
```

## SPI Layer (`dice_spi.h`)

`SPIDriver` owns a fixed pool of 16 DMA-capable `SPIBuffer` objects, each 8 KB. All buffers are pre-queued into the `ESP32DMASPI::Slave` hardware queue at startup. When the SPI hardware finishes a transaction it sends a FreeRTOS task notification to the `SPI_Decode` task. The decode task drains completed transactions with `slave.takeResult()`, forwards each buffer to `DecodingHandler::enqueue_raw_buffer()`, then requeues the buffer back to the SPI hardware as soon as processing is complete. No data is copied; only `SPIBuffer*` pointers travel between tasks.

## Protocol Layer (`protocol.h` + `constants.h`)

Every SPI packet begins with a 5-byte header:

```
Byte 0:   SOF_MARKER (0x7E)
Byte 1:   MessageType (uint8)
Byte 2:   screenId bitmask
Bytes 3-4: payload length (big-endian uint16)
```

Screen ID filtering works like a CAN-bus bitmask. The global `SCREEN_ID` (default 6) indicates which bit in the `screenId` byte this board listens on. A message is accepted only if `(screenId >> SCREEN_ID) & 1 == 1`. This lets the Pi broadcast one packet that reaches multiple boards simultaneously.

### Message Types

| Type | Value | Description |
|---|---|---|
| `TEXT_BATCH` | 0x01 | One or more text items with position, font, and color |
| `IMAGE_TRANSFER_START` | 0x02 | Image metadata + embedded chunk 0 |
| `IMAGE_CHUNK` | 0x03 | Subsequent JPEG/RGB565 data chunk |
| `IMAGE_TRANSFER_END` | 0x04 | Optional end-of-transfer marker |
| `BACKLIGHT_ON` / `OFF` | 0x0A/0x0B | Backlight control, no payload |
| `PING_REQUEST` | 0x0C | Connectivity check, empty payload |
| `PING_RESPONSE` | 0x0D | Status string reply |
| `VIDEO_STREAM_INIT` | 0x10 | H.264 codec config (SPS/PPS) |
| `VIDEO_FRAME_START` | 0x11 | Frame metadata + embedded chunk 0 |
| `VIDEO_FRAME_CHUNK` | 0x12 | Continuation chunk for current frame |
| `VIDEO_STREAM_END` | 0x13 | Graceful end of stream |
| `VIDEO_FLUSH` | 0x14 | Emergency pipeline drain |
| `SET_ROTATION` | 0x15 | Change display rotation |

## Chunked Image Transfer

JPEG files are too large to fit in a single 8 KB SPI buffer. The Pi splits the file into fixed-size chunks and sends them sequentially:

1. `IMAGE_TRANSFER_START` — carries metadata (image ID, format, resolution, total size, chunk count, rotation) and embeds chunk 0 in its payload.
2. `IMAGE_CHUNK` × (N-1) — carries subsequent chunk data with a chunk ID and byte offset.
3. `IMAGE_TRANSFER_END` — optional; the firmware also detects completion by counting received chunks.

`DecodingHandler` tracks in-progress transfers in an `ongoing_transfers` map keyed by image ID. When `received_chunks[imgId] >= expected_chunks[imgId]`, the `Image` object is handed to the screen queue. Single-chunk images are complete after the `IMAGE_TRANSFER_START` message alone.

## JPEG Decode Pipeline (`media.h` / `media.cpp`)

The `Image` class allocates raw JPEG bytes in internal RAM and a decoded RGB565 frame buffer in PSRAM. Once all chunks have arrived, `Image::startDecode()` spawns a dedicated FreeRTOS task that calls the JPEGDEC library. JPEGDEC invokes `JPEGDraw480` or `JPEGDraw240` callbacks for each 16x16 MCU block, writing RGB565 pixels directly into the PSRAM buffer. When decoding finishes the image status advances to `READY` and `Screen::update()` can display it.

For 240x240 images (`SQ240` resolution), the firmware upscales to 480x480 using a 2x nearest-neighbour algorithm (`upscale_bmp565_2x`) before pushing to the panel.

## Text Rendering (`media.h`, U8g2)

A `TEXT_BATCH` message produces a `TextGroup` containing one or more `Text` objects. Each `Text` carries a string, (x, y) cursor position, font ID, and an RGB565 color. `Screen::draw_textgroup()` fills the background, then renders each text item via U8g2 using font pointers selected by `Text::map_font()`:

| FontID | U8g2 font |
|---|---|
| `TF` | `u8g2_font_unifont_tf` |
| `ARABIC` | `u8g2_font_unifont_t_arabic` |
| `CHINESE` | `u8g2_font_unifont_t_chinese3` |
| `CYRILLIC` | `u8g2_font_cu12_t_cyrillic` |
| `DEVANAGARI` | `u8g2_font_unifont_t_devanagari` |

Rotation is applied through `Screen::map_text_rotation()`, which swaps 90° and 270° to compensate for the physical display orientation.

## Display Driver (Arduino_GFX)

`Screen` holds an `Arduino_Canvas` (framebuffer in PSRAM) and pushes completed frames to the panel via `esp_lcd_panel_rgb`. The `Arduino_XCA9554SWSPI` I/O expander is used for SPI communication with the panel's configuration registers. The 60 Hz update loop calls `screen->update()` every 16 ms from the Arduino `loop()` function.

## Media Container Hierarchy

All displayable content is a subclass of `MediaContainer` in the `dice` namespace:

- `TextGroup` — a collection of `Text` items sharing a background color and rotation. Iterated with `get_next()`.
- `Text` — a single string with position, font, and color.
- `Image` — a JPEG or RGB565 image; decodes asynchronously via a FreeRTOS task into a PSRAM buffer.
- `VideoFrame` — a single H.264-decoded frame stored as RGB565 in a PSRAM pool slot; returned to the pool on destruction.

`Screen` owns all enqueued `MediaContainer*` objects and deletes them after display.
