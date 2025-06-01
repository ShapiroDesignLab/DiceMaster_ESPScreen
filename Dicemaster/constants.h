#ifndef DICE_CONSTANTS_H
#define DICE_CONSTANTS_H

#include <vector>
#include <string>
#include <map>


namespace dice {

// Enums for Media Status
enum class MediaStatus : uint8_t {
    NOT_RECEIVED = 0,
    DECODING = 2,
    READY = 3,
    DISPLAYING = 4,
    EXPIRED = 5
};

// Enums for Media Types
enum class MediaType : uint8_t {
    TEXT = 0,
    TEXTGROUP = 1,
    IMAGE = 2,
    OPTION = 3,
    GIF = 5,
    CTRL = 255
};

// Enums for Image Formats
enum class ImageFormat : uint8_t {
    NOIMG = 0,
    JPEG=1,
    RGB565=2,
    RGB222=3
};

enum class ImageResolution: uint8_t {
    SQ480=1,
    SQ240=2
};

// Enums for Font IDs
enum class FontID : uint8_t {
    NOTEXT=0,
    TF = 1,
    ARABIC=2,
    CHINESE=3,
    CYRILLIC=4,
    DEVANAGARI=5
};

enum class PrettyColor : uint32_t {
    DARKGREY = 0x636363,
    BABYBLUE = 0xbee3f5,
    BLACK = 0x000000,
    WHITE = 0xffffff
};

enum class Command : uint8_t {
    BACKLIGHT_OFF =1,
    BACKLIGHT_ON=2,
    OPTION_ID=3
};

// Screen Buffer Size
constexpr size_t SCREEN_PXLCNT = 480 * 480;

// Enums for Message Types
enum class MessageType : uint8_t {
    TEXT_BATCH = 0x01,
    IMAGE_TRANSFER_START = 0x02,
    IMAGE_CHUNK = 0x03,
    IMAGE_TRANSFER_END = 0x04,
    OPTION_LIST = 0x05,
    OPTION_SELECTION_UPDATE = 0x06,
    GIF_TRANSFER_START = 0x07,
    GIF_FRAME = 0x08,
    GIF_TRANSFER_END = 0x09,
    BACKLIGHT_ON = 0x0A,
    BACKLIGHT_OFF = 0x0B,
    ACK = 0xEF,
    ERROR = 0xEE
};

// Enums for Error Codes
enum class ErrorCode : uint8_t {
    SUCCESS = 0x00,
    UNKNOWN_MSG_TYPE = 0x01,
    INVALID_FORMAT = 0x02,
    IMAGE_ID_MISMATCH = 0x04,
    PAYLOAD_LENGTH_MISMATCH = 0x05,
    UNSUPPORTED_IMAGE_FORMAT = 0x06,
    OUT_OF_MEMORY = 0x07,
    INTERNAL_ERROR = 0x08,
    INVALID_OPTION_INDEX = 0x09
};

// SPI Protocol constants
constexpr uint8_t SOF_MARKER = 0x7E;

}

#endif