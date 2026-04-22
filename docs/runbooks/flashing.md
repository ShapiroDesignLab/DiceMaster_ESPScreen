# Runbook: Flashing Firmware

## Prerequisites

**Hardware:**
- Adafruit Qualia ESP32-S3 board connected via USB-C to your development machine.
- USB cable that carries data (not charge-only).

**Software:**
- Arduino IDE 2.x (2.3 or later recommended).
- `esp32` board support package **3.2.0** installed via the Arduino Boards Manager.
  - In Arduino IDE: Tools → Board → Boards Manager → search "esp32 by Espressif Systems" → install version 3.2.0.
- The following libraries installed via the Arduino Library Manager (Tools → Manage Libraries):
  - **ESP32DMASPI** 0.8.0
  - **GFX Library for Arduino** 1.6.5
  - **JPEGDEC** 1.8.4
  - **U8g2** 2.35.30

Exact versions matter. The firmware uses APIs from these specific releases; newer versions may introduce breaking changes.

## Steps

1. **Open the sketch.**
   In Arduino IDE, go to File → Open and select `Dicemaster/Dicemaster.ino`. The IDE will open all companion `.h` and `.cpp` files in the same directory as tabs.

2. **Select the board.**
   Tools → Board → ESP32 Arduino → **Adafruit Feather ESP32-S3 No PSRAM** is the closest match; however the correct target is the Adafruit Qualia ESP32-S3. If the Qualia board definition is available in your esp32 3.2.0 install, select it directly: Tools → Board → ESP32 Arduino → **Adafruit Qualia ESP32-S3**.

3. **Set the partition scheme.**
   Tools → Partition Scheme → **Huge APP (3MB No OTA / 1MB SPIFFS)** or an equivalent scheme that allocates at least 3 MB to the application partition. The firmware binary is large due to bundled font data (U8g2) and JPEGDEC.

4. **Set upload speed.**
   Tools → Upload Speed → **921600** for faster flashing. If upload fails, reduce to 460800.

5. **Select the port.**
   Tools → Port → select the USB serial port corresponding to the connected board. On macOS this appears as `/dev/cu.usbmodem*`; on Linux as `/dev/ttyACM*` or `/dev/ttyUSB*`.

6. **Verify (optional but recommended).**
   Click the checkmark (Verify/Compile) button to confirm the sketch compiles without errors before uploading.

7. **Upload.**
   Click the arrow (Upload) button. The IDE compiles the sketch and flashes it over USB. Progress is shown in the output panel.

## Verify

1. Open the Serial Monitor: Tools → Serial Monitor (or Ctrl+Shift+M).
2. Set baud rate to **115200**.
3. Press the reset button on the board (or power-cycle it).
4. You should see the following boot sequence in the serial monitor:

   ```
   Begin DiceMaster Screen Module
   PSRAM correctly initialized
   [SPI] Allocated buffer 0 with ID 0
   ...
   [SPI] Allocated buffer 15 with ID 15
   [SPI] Initialized with 16 DMA buffers for event-driven pipeline
   [DECODE] Initialized event-driven handler with dedicated processing task
   [SPI] Event-driven pipeline initialized successfully in constructor
   === DiceMaster System Ready ===
   Current mode: PRODUCTION
   System initialized successfully.
   ```

5. The display should show the startup logo for approximately 2 seconds, then switch to a pulsing loading-dots animation while waiting for SPI commands from the Pi master.

## Troubleshooting

**Upload fails immediately with "Failed to connect to ESP32-S3"**
The board may be in a crash loop from a previous bad firmware upload. Perform a factory reset (see below) before trying again.

**Factory reset procedure:**
The file `resource/Qualia_S3_RGB666_FactoryReset.uf2` in this repository contains the Adafruit factory firmware. To restore it:
1. Double-tap the reset button on the Qualia board quickly. The board will enter UF2 bootloader mode and appear as a USB mass storage device named `BOOT`.
2. Drag `resource/Qualia_S3_RGB666_FactoryReset.uf2` onto the `BOOT` drive.
3. The board reboots into factory firmware. USB upload from Arduino IDE will then work again.

**Compilation error: library not found**
Confirm the exact library version is installed. The Library Manager may have installed a different version. Check Sketch → Include Library → Manage Libraries and verify each version matches the prerequisites.

**"PSRAM not available" in serial monitor**
This indicates a hardware or board selection mismatch. Confirm the correct ESP32-S3 board with PSRAM is selected in Tools → Board. The Adafruit Qualia ESP32-S3 has 8 MB of PSRAM; if it is not detected the firmware will crash when allocating the RGB565 framebuffer.

**Screen is blank after boot**
Check the serial monitor for `[SPI]` initialization messages. If the DMA buffer allocation fails for any buffer, the SPI pipeline will not start. Ensure the correct partition scheme is selected so the firmware binary fits within the application partition.
