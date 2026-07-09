# DiceMaster_ESPScreen

ESP32 Arduino firmware for one screen-face of the [DiceMaster](https://github.com/DanielHou315/DiceMaster) programmable dice. Six of these boards are embedded in the dice; each receives compressed media frames from the Raspberry Pi over SPI and drives a 480×480 IPS LCD.

For system architecture, see the [root repository](https://github.com/DanielHou315/DiceMaster).

## How it fits in the system

```
Raspberry Pi (DiceMaster_Central)
  │
  │  SPI — chunked protocol (see docs/protocol.md in root repo)
  ▼
ESP32-S3 (this repo) × 6
  │
  ▼
480×480 IPS LCD (Adafruit Qualia ESP32-S3)
```

## Hardware

- Board: [Adafruit Qualia ESP32-S3 for RGB-666 Displays](https://www.adafruit.com/product/5800) — one per screen face
- Display: 480×480 round or square IPS LCD panel (RGB-666 interface, driven by the Qualia)
- Connection to Pi: SPI (slave mode) — see [SPI pin table](#spi-wiring) below
- Power: supplied from the dice mainboard

### SPI Wiring

`spi.h` calls `slave.begin()` with no explicit pin override, so the driver uses the ESP32-S3 HSPI (SPI2) hardware defaults. Confirm physical wiring against the dice mainboard schematic.

| Signal | ESP32-S3 Pin | Raspberry Pi Pin |
|--------|-------------|-----------------|
| MOSI   | TBD (HSPI default GPIO 11) | GPIO 10 (SPI0 MOSI) |
| MISO   | TBD (HSPI default GPIO 13) | GPIO 9  (SPI0 MISO) |
| SCLK   | TBD (HSPI default GPIO 12) | GPIO 11 (SPI0 SCLK) |
| CS     | TBD (HSPI default GPIO 10) | GPIO 8  (SPI0 CE0)  |

## Installation

### Requirements
- Arduino IDE 2.x or PlatformIO
- ESP32 Arduino Core 3.2.0
- Board: "Adafruit Feather ESP32-S3 No PSRAM" or "ESP32S3 Dev Module"
- **Required board settings**: PSRAM: "OPI PSRAM", Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
- Required libraries (install via Arduino Library Manager):
  - U8g2 2.35.30 (font rendering)
  - JPEGDEC 1.8.4 (JPEG decode)
  - ESP32DMASPI 0.8.0 (SPI slave with DMA)
  - GFX Library for Arduino 1.6.5 (display driver)

### Flashing
See `docs/runbooks/flashing.md` for step-by-step flash instructions including board selection, partition scheme, and factory reset procedure.

**Factory reset** (needed after a crash/segfault that prevents USB uploads):
Drag `resource/Qualia_S3_RGB666_FactoryReset.uf2` onto the board's USB drive.

**Serial monitor**: 115200 baud.

### Development workflow
See `docs/runbooks/local-dev.md` for compile/flash cycle, serial log format, and end-to-end testing with the Pi.

## Documentation

| Doc | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | How the firmware works: system modes, SPI pipeline, wire protocol, media types, config |
| [docs/setup.md](docs/setup.md) | Toolchain, exact library versions, board settings |
| [docs/runbooks/flashing.md](docs/runbooks/flashing.md) | Build & flash a board, factory reset, troubleshooting |
| [docs/runbooks/local-dev.md](docs/runbooks/local-dev.md) | Compile/flash/debug loop, serial logs, `SPI_DEBUG`, end-to-end test |
| root repo `docs/protocol.md` | The SPI wire protocol, shared with `DiceMaster_Central` |

The README and `docs/` are the single source of truth. `CLAUDE.md` / `AGENTS.md`
are thin pointers here — coding agents should read the docs, not a separate copy.

## Repository layout

```
Dicemaster/              # Main Arduino sketch directory
  ├── Dicemaster.ino     # Entry point (setup/loop)
  ├── screen.h/cpp       # Display abstraction (render pipeline)
  ├── spi.h              # SPI slave + DMA transport (SPIDriver)
  ├── decoding_handler.h # Protocol message dispatcher
  ├── media.h/cpp        # MediaContainer types (image, text, GIF)
  ├── protocol.h         # Wire format — shared with Central
  ├── constants.h        # Enums: MessageType, MediaType, Priority
  ├── bmp.hs/            # BMP asset headers (pre-encoded bitmaps)
  └── jpg.hs/            # JPEG asset headers (pre-encoded images)
scripts/                 # Host-side asset processing tools
docs/
  └── setup.md           # Initial setup notes
resource/                # Factory-reset UF2 and other board assets
```

Note: `.hs` directories contain C header files with embedded binary assets, not Haskell source.
