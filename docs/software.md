# Programmable Dice Software Reference

## Protocol Design

The Raspberry Pi and the Screen Modules communicate with each other in SPI through packets that are formatted as follows. 

**Message Header**

This section of bytes is attached to each packet, storing the necessary statistics and message types for parsing of the rest of the message. 

MOSI: 2048 Bytes (2kb buffer, 480x480 jpegs are mostly 10~15kb)
Command Messages: 6 bytes
- BYTE 0: Start of Frame (SOF): 0x7E
- BYTE 1: Message Type
- BYTE 2: Message ID
- BYTE 3-4: Payload length (BIG_ENDIAN)
- The payload starts at BYTE 5 onwards. 

**Message Types**
- 0x01 - Text Batch
- 0x02 - Image Transfer Start
- 0x03 - Image Chunk
- 0x04 - Image Transfer End
- 0x05 - Option List
- 0x06 - Option Selection Update
- 0x07 - GIF Transfer Start
- 0x08 - GIF Frame
- 0x09 - GIF Transfer End
- 0x0A - Backlight Control
- 0x0B - Acknowledgment (ACK)
- 0x0C - Error Message (NACK)

The payload of the message is different for different types of messages, described as follows. 

### Text Section
**Text Group**
A text group 
- BYTE 0-1: BG Color
- BYTE 2-3: Font Color
- BYTE 4: number of lines
- Individual chunks 

**Text**

- BYTE 0-1: x cursor
- BYTE 2-3: y cursor
- BYTE 4: font id
- BYTE 5: text length
- PAYLOAD STRING
**NOTE** each text block can be NO LONGER THAN 255 BYTES due to size byte limit. 

### Images
**Image Begin**
- BYTE 0: image ID
- BYTE 1: 4-bit Format, 4-bit Resolution
- BYTE 2: Delay Time (0-255 ms)
- BYTE 3-5: total image size
- BYTE 6: num chunks

**IMAGE CHUNK**
- BYTE 0: image ID
- BYTE 1: chunk ID
- BYTE 2-4: starting location
- BYTE 5-6: length of chunk (max 65535 Bytes)
- PAYLOAD

**IMAGE END**
- BYTE 0: Image ID

**GIF BEGIN**
- BYTE 0: image ID
- BYTE 1: 4-bit Format, 4-bit Resolution
- BYTE 2: Delay Time (0-255 ms)
- BYTE 3-5: total image size
- BYTE 6: num chunks

**IMAGE_CHUNK**
- BYTE 0: image ID
- BYTE 1: chunk ID
- BYTE 2-5: starting location
- BYTE 6-7: length of chunk
- PAYLOAD

**IMAGE_END**
- BYTE 0: Image ID
    - Draw Settings Menu Option:
	- 1 byte:selecting
- 2-3 bytes: upper option x
	- 4-5 bytes: upper option y
	- 6 byte: upper option text length
	- 7 byte onward: actual text

### Options

## File Structure

Raspberry PI SD Card Folder Requirements: 
SD_ROOT → activity

SD_ROOT
| – 2024FA-GER-354
|	| – 20240826
| 	|	| – activity-1
|	| – 20240827-20240828
| 	|	| – activity-1
| 	|	| – activity-2
| – ENG-125-002-FA24
|	| – discussion-15
| 	|	| – activity-1
| 	|	| – activity-2

## Supported Languages



# Developing Software

## For Screen Modules

**Factory Reset**

- It is inevitable that you will upload some program that cause segmentation faults. In this case, the board hard-crashes and is unable to receive your further uploads through the USB port. To factory-reset the board and erase the existing (buggy) program, drag over the factory default UF2 file located in the `resources/` directory. 

**Dependencies**
- esp32 (2.0.17)
- 
- ESP32DMASPI (0.6.5)
- GFX Library for Arduino (1.4.9)
- JPEGDEC (1.8.2)
- U8g2 (2.35.30)

## For Raspberry Pi
- Flash 
- Setup SSH
