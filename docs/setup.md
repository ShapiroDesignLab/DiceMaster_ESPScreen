# ESPScreen Setup

## Toolchain

- Arduino IDE 2.x (or PlatformIO)
- ESP32 Arduino Core **3.2.0** (Boards Manager → "esp32")

## Library dependencies (exact versions)

| Library | Version |
|---|---|
| ESP32DMASPI | 0.8.0 |
| GFX Library for Arduino | 1.6.5 |
| JPEGDEC | 1.8.4 |
| U8g2 | 2.35.30 |

> Versions matter — the SPI-slave and display APIs changed across releases.
> These are the versions the current firmware is built and tested against, and
> they match `../README.md` and `docs/runbooks/flashing.md`. Do not mix versions.

## Board settings

- **Board:** "Adafruit Feather ESP32-S3 No PSRAM" (or "ESP32S3 Dev Module")
- **PSRAM:** `OPI PSRAM` (required)
- **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)` (required)

## Flashing & factory reset

Full step-by-step flashing instructions, including recovery from a crashed board
(drag `resource/Qualia_S3_RGB666_FactoryReset.uf2` onto the board's USB drive),
are in **[docs/runbooks/flashing.md](runbooks/flashing.md)**.

## Hardware

- [Adafruit Qualia ESP32-S3 for RGB-666 Displays](https://www.adafruit.com/product/5800)
- 480×480 IPS LCD panel (RGB-666)
- [Board guide](https://learn.adafruit.com/adafruit-qualia-esp32-s3-for-rgb666-displays)

## Next steps

- [docs/architecture.md](architecture.md) — how the firmware works
- [docs/runbooks/local-dev.md](runbooks/local-dev.md) — compile/flash/debug loop
