# Runbook: Local Development Workflow

## Prerequisites

- Arduino IDE 2.x with all dependencies installed as described in `docs/runbooks/flashing.md`.
- Adafruit Qualia ESP32-S3 board connected via USB.
- Serial monitor open at 115200 baud.
- (Optional) Pi master available on the same SPI bus for end-to-end testing.

## Steps

### 1. Open the Project

Open `Dicemaster/Dicemaster.ino` in Arduino IDE. The IDE loads all companion files as tabs:

- `constants.h` — enums, color constants, `SCREEN_ID`, `MessageType`, `ErrorCode`.
- `protocol.h` — binary SPI protocol encode/decode (header + all payload types).
- `dice_spi.h` — `SPIDriver`: DMA buffer pool, SPI slave setup, decode task.
- `decoding_handler.h` — `DecodingHandler`: message routing, image chunk assembly, screen enqueueing.
- `media.h` / `media.cpp` — `Image`, `Text`, `TextGroup`, `VideoFrame` media containers.
- `screen.h` / `screen.cpp` — `Screen`: FreeRTOS media queue, 60 Hz render loop, Arduino_GFX display driver.
- `examples.h` — demo sequences used in `DEMO` mode.
- `tests.h` — `TestSuite` used in `TESTING` mode.

### 2. Configure Board Settings

In Arduino IDE Tools menu, confirm:

- **Board**: Adafruit Qualia ESP32-S3 (or closest match under ESP32 Arduino).
- **Partition Scheme**: Huge APP (3MB No OTA / 1MB SPIFFS).
- **Port**: the USB serial port of the connected board.

### 3. Select a Development Mode

Edit `Dicemaster.ino` before flashing. Change the `current_mode` variable at the top of the file:

```cpp
// For unit/integration tests:
SystemMode current_mode = SystemMode::TESTING;

// For visual demos without a Pi:
SystemMode current_mode = SystemMode::DEMO;

// For SPI statistics and throughput measurement:
SystemMode current_mode = SystemMode::SPI_DEBUG;

// For normal production operation:
SystemMode current_mode = SystemMode::PRODUCTION;
```

If you are changing only this variable (not any `.h` files), a recompile and re-flash is required.

You may also change `SCREEN_ID` to select which bit in the SPI `screenId` bitmask this board responds to:

```cpp
uint8_t SCREEN_ID = 6;  // default; board listens when bit 6 is set in the screenId byte
```

And `DEFAULT_ROTATION` to adjust the display orientation:

```cpp
Rotation DEFAULT_ROTATION = Rotation::ROT_0;  // ROT_0, ROT_90, ROT_180, or ROT_270
```

### 4. Compile

Click the checkmark (Verify/Compile) button to build without uploading. Fix any compilation errors before proceeding. Common issues:

- Missing library: install the exact version listed in the flashing runbook.
- `PSRAM` symbol not found: ensure the `esp32` board package version is exactly 3.2.0.

### 5. Flash

Click the Upload (arrow) button. The IDE compiles and flashes the firmware. If the board is in a crash loop, perform the factory reset procedure described in `docs/runbooks/flashing.md` first.

### 6. Monitor Debug Output

Open Tools → Serial Monitor, set baud rate to **115200**. Key log prefixes and their sources:

| Prefix | Source |
|---|---|
| `[SPI]` | `SPIDriver` — buffer allocation, transaction counts |
| `[SPI-DECODE]` | Decode task — transaction errors, buffer forwarding |
| `[SPI-REQUEUE]` | Buffer requeue path — mutex acquisition, requeue status |
| `[DECODE]` | `DecodingHandler` — message parsing, chunk assembly, enqueue result |
| `[DECODE-TASK]` | Processing task — startup, notification receipt |
| `[TEXT BATCH]` | Text message decoding detail |
| `[IMAGE-TASK]` | Per-image async decode task |
| `[DEBUG]` | `SPI_DEBUG` mode periodic stats (transaction count, decode count, free heap) |

In `SPI_DEBUG` mode the serial monitor prints a statistics line every 3 seconds:

```
[DEBUG] T:142 D:18 F:0 Q:0 S:18 H:234KB P:7124KB Overflows:0
```

Fields: T = total SPI transactions, D = messages decoded, F = decode failures, Q = current raw queue depth, S = media items sent to screen, H = free heap, P = free PSRAM, Overflows = raw buffer queue overflow count.

### 7. Add or Modify Image Assets

The `test_images/` directory holds source images. Helper scripts in `scripts/` convert them to C header files:

```bash
# Crop and resize a single image to 480x480 JPEG
python3 scripts/img_processor.py test_images/foo.jpg

# Crop and resize a GIF or video to 240x240 JPEG frames
python3 scripts/img_processor.py test_images/animation.gif

# Convert an image to an RGB565 C header (for bmp.hs/)
python3 scripts/img2rgb565.py test_images/foo.jpg

# Convert a JPEG to a raw byte-array C header (for jpg.hs/)
python3 scripts/img2bytes.py test_images/foo.jpg
```

After generating a header, include it in `examples.h` and add a call to the relevant demo sequence in `run_demo_sequence()`, or reference it in a `TestSuite` test case.

### 8. Test with the Pi Master

To test the full SPI pipeline:

1. Set `current_mode = SystemMode::PRODUCTION` and flash the board.
2. On the Pi, use the DiceMaster Central scripts or test utilities to send `TEXT_BATCH` or `IMAGE_TRANSFER_START` / `IMAGE_CHUNK` SPI commands addressed to the correct `SCREEN_ID` bit.
3. Watch the serial monitor for `[DECODE] SUCCESS: Image ID ... transfer complete` or `[DECODE] DEBUG: Creating TextGroup` to confirm the Pi-side encoding and the board-side decode are aligned.
4. If images appear corrupted, check that chunk sizes on the Pi side do not exceed 8 KB and that the `numChunks` field in `IMAGE_TRANSFER_START` matches the actual number of packets sent.

## Verify

After any firmware change:

1. Serial monitor shows `=== DiceMaster System Ready ===` and the correct mode name on boot.
2. In `TESTING` mode, the serial monitor shows `=== TESTING COMPLETE ===` and no assertion failures.
3. In `DEMO` mode, the display cycles through text and image examples without crashing.
4. In `PRODUCTION` mode with the Pi connected, the serial monitor shows `media_enqueued_to_screen` incrementing when the Pi sends display commands.

## Troubleshooting

**Screen freezes after a few minutes**
Check free PSRAM with `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` in `SPI_DEBUG` mode. A memory leak in image objects (e.g. failed `delete` after a rejected `screen->enqueue()`) will exhaust PSRAM. Ensure all code paths that create a `MediaContainer` either successfully enqueue it (transferring ownership to `Screen`) or delete it:

```cpp
if (!screen->enqueue(media_obj)) delete media_obj;
```

**Text appears at wrong rotation**
`Screen::map_text_rotation()` swaps 90° and 270° to compensate for the physical display orientation. If text is rotated unexpectedly, check the `rotation` byte in the `TEXT_BATCH` message from the Pi; the firmware maps `ROT_90 ↔ ROT_270` internally.

**`IMAGE_CHUNK for unknown image ID` in serial log**
The Pi sent a chunk before the corresponding `IMAGE_TRANSFER_START`, or the start message had a `screenId` bitmask that did not include this board's `SCREEN_ID` bit, so the START was silently dropped. Verify the Pi-side screen ID encoding.
