# ESP32 Screen Module Setup

## Dependencies

- esp32 (3.2.0)
- ESP32DMASPI (0.8.0)
- GFX Library for Arduino (1.6.5)
- JPEGDEC (1.8.4)
- U8g2 (2.35.30)

## Factory Reset

It is inevitable that you will upload a program that causes segmentation faults. In this case, the board hard-crashes and cannot receive further uploads through USB. To factory-reset, drag the default UF2 file from `resource/` onto the board.

## Hardware

- [Adafruit LCD 4" square display](https://www.adafruit.com/product/5827)
- [ESP32-S3 controller](https://www.adafruit.com/product/5800)
- [Board guide](https://learn.adafruit.com/adafruit-qualia-esp32-s3-for-rgb666-displays)
