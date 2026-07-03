# Runbook: Flashing a Board

How to build and flash the DiceMaster firmware onto an Adafruit Qualia ESP32-S3
board, and how to recover a board that won't accept uploads.

## Prerequisites

- Arduino IDE 2.x (or PlatformIO)
- **ESP32 Arduino Core 3.2.0** (Boards Manager → "esp32" by Espressif)
- Required libraries at these exact versions (Library Manager):

  | Library | Version |
  |---|---|
  | U8g2 | 2.35.30 |
  | JPEGDEC | 1.8.4 |
  | ESP32DMASPI | 0.8.0 |
  | GFX Library for Arduino | 1.6.5 |

  > Versions matter — the SPI slave and display APIs have changed across releases.
  > See `docs/setup.md` for the dependency list and `../README.md` for context.

- A USB-C cable and the board.

## Steps

### 1. Open the sketch

Open `Dicemaster/Dicemaster.ino` in the Arduino IDE.

### 2. Set the board and options

- **Board:** "Adafruit Feather ESP32-S3 No PSRAM" (or "ESP32S3 Dev Module")
- **PSRAM:** `OPI PSRAM`  *(required — image decode uses PSRAM)*
- **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`  *(required — the
  firmware + embedded assets do not fit the default partition)*

### 3. Set this board's identity

In `Dicemaster.ino`, set:

```cpp
uint8_t SCREEN_ID = <0..5>;                       // unique per board, matches its dice face
SystemMode current_mode = SystemMode::PRODUCTION; // or DEMO for a standalone bench test
```

Each of the six boards gets a **distinct `SCREEN_ID`**. See
`docs/architecture.md` → "Wire protocol" for how the ID is used.

### 4. Select the port and upload

Plug in the board over USB, pick its serial port, and click Upload.

### 5. Verify

Open the Serial Monitor at **115200 baud**. On boot you should see `[SPI]`
initialization lines (buffer allocation, "Event-driven pipeline initialized").
In `DEMO` mode the display should also start cycling content.

## Factory reset (recovering a bricked board)

It is normal to eventually upload firmware that segfaults on boot. When that
happens the board hard-crashes before USB enumerates and **cannot receive
further uploads**. To recover:

1. Put the board into its UF2 bootloader (it presents a USB drive). On the
   Qualia this is typically double-tap reset; consult the
   [Adafruit Qualia guide](https://learn.adafruit.com/adafruit-qualia-esp32-s3-for-rgb666-displays).
2. Drag `resource/Qualia_S3_RGB666_FactoryReset.uf2` onto that USB drive.
3. The board reboots to factory firmware; you can now upload again.

## Troubleshooting

| Problem | Fix |
|---|---|
| Upload hangs / port disappears | Board likely crashed on boot — do a **factory reset** (above). |
| Compiles but nothing on screen | Wrong PSRAM or partition setting — recheck step 2. |
| "Sketch too big" | Partition scheme isn't "Huge APP" — recheck step 2. |
| Garbled display / wrong colors | Library version mismatch (GFX / ESP32DMASPI) — reinstall exact versions. |
| Board boots but ignores commands | `SCREEN_ID` doesn't match what Central addresses, or `current_mode` isn't `PRODUCTION`. |
