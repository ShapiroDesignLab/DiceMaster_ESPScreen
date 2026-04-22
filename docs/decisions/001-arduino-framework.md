# ADR-001: Arduino Framework for ESP32 Firmware

## Status

Accepted

## Context

The DiceMaster_ESPScreen firmware runs on the Adafruit Qualia ESP32-S3 and must:

- Drive a 480x480 RGB666 LCD panel via the ESP32-S3's RGB LCD peripheral.
- Decode JPEG images in real time using a DMA-backed SPI slave interface.
- Render multi-language text (Arabic, Chinese, Cyrillic, Devanagari, Latin) from a FreeRTOS task.
- Be flashable and debuggable with minimal toolchain setup by a small student team at the U-M Shapiro Design Lab.

The team had prior experience with Arduino-style C++ sketches and embedded projects using Arduino-compatible libraries. Selecting a firmware framework was therefore primarily a question of ecosystem fit and development speed.

## Decision

Use the Arduino framework via the **arduino-esp32** board support package (version 3.2.0), targeting the Adafruit Qualia ESP32-S3 board definition. The project is structured as a standard Arduino sketch (`Dicemaster/Dicemaster.ino`) with companion header files.

Key libraries that drove this decision, all available as Arduino library manager packages:

- **ESP32DMASPI 0.8.0** — SPI slave with DMA buffer management; provides the `ESP32DMASPI::Slave` and `SPISlaveBuffer` types used throughout `dice_spi.h`.
- **GFX Library for Arduino (Arduino_GFX) 1.6.5** — display driver supporting the `Arduino_Canvas` framebuffer and `esp_lcd_panel_rgb` integration used in `screen.h`.
- **JPEGDEC 1.8.4** — single-header JPEG decoder with a callback-based MCU draw interface (`JPEGDraw480`, `JPEGDraw240`) that writes decoded blocks directly into a PSRAM buffer without an intermediate allocation.
- **U8g2 2.35.30** — bitmap font library with bundled multi-language glyphs used in `Text::map_font()`.

FreeRTOS is available transparently through arduino-esp32, allowing the firmware to create tasks (`SPI_Decode`, `DecodingProcessor`, per-image decode tasks) and synchronisation primitives (`xQueueCreate`, `xSemaphoreCreateMutex`, `xTaskNotifyGive`) without leaving the Arduino build system.

## Consequences

**Benefits:**

- All required libraries (JPEGDEC, U8g2, Arduino_GFX, ESP32DMASPI) are installable through the Arduino Library Manager; no manual CMake configuration or submodule management is required.
- The Arduino serial monitor at 115200 baud is available immediately for debug output; the firmware uses `Serial.println()` throughout all pipeline components.
- Factory-reset recovery is a drag-and-drop UF2 file operation, which is important because segfaults from incomplete JPEG decode buffers can brick USB upload capability.
- Full FreeRTOS access is retained; the firmware uses task priorities, DMA memory (`heap_caps_malloc` / `MALLOC_CAP_SPIRAM`), and task notifications without restriction.

**Trade-offs:**

- Less direct control over linker scripts, partition tables, and build flags compared to a bare ESP-IDF CMake project. The partition scheme must be set in the Arduino IDE board menu rather than in a `partitions.csv` file in the repository.
- arduino-esp32 wraps certain ESP-IDF APIs; any future dependency on a very recent IDF feature may require a board package upgrade.

## Alternatives Considered

**Bare ESP-IDF (CMake)**
Full control over the build, partition layout, and component versions. Rejected because it requires significant toolchain setup (`idf.py`, Python venv, xtensa toolchain) that would slow onboarding for a student team, and the required libraries (JPEGDEC, U8g2, Arduino_GFX) do not have ESP-IDF component versions.

**MicroPython**
Rejected early. JPEG decoding and SPI DMA throughput requirements exceed what the MicroPython runtime can sustain. The 480x480 framebuffer (460 KB RGB565) and real-time decode task would compete for heap with the interpreter.

**CircuitPython**
Similarly rejected for performance reasons. CircuitPython's displayio subsystem targets lower-resolution displays and does not expose the DMA SPI slave peripheral needed for the receive-side protocol.
