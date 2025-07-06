#ifndef DICE_CONSTANTS_H
#define DICE_CONSTANTS_H

#include <vector>
#include <string>
#include <map>


// Enums for Media Status
enum class MediaStatus : uint8_t {
    NOT_RECEIVED = 0,
    DECODING = 2,
    READY = 3,
    DISPLAYING = 4,
    EXPIRED = 5
};

// Enums for System Operating Mode
enum class SystemMode : uint8_t {
    TESTING = 0,
    DEMO = 1,
    PRODUCTION = 2
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

// Enums for Rotation
enum class Rotation : uint8_t {
    ROT_0 = 0,      // 0 degrees
    ROT_90 = 1,     // 90 degrees clockwise
    ROT_180 = 2,    // 180 degrees
    ROT_270 = 3     // 270 degrees clockwise (90 counter-clockwise)
};

// Common color constants (avoid conflicts with Arduino GFX library)
constexpr uint16_t DICE_DARKGREY = 0x6B6D;
constexpr uint16_t DICE_BABYBLUE = 0xDF1C;
constexpr uint16_t DICE_BLACK = 0x0000;
constexpr uint16_t DICE_WHITE = 0xFFFF;
constexpr uint16_t DICE_RED = 0xF800;
constexpr uint16_t DICE_GREEN = 0x07E0;
constexpr uint16_t DICE_BLUE = 0x001F;

enum class Command : uint8_t {
    BACKLIGHT_OFF =1,
    BACKLIGHT_ON=2,
    OPTION_ID=3
};

// Screen Buffer Size
constexpr size_t SCREEN_PXLCNT = 480 * 480;

// Enums for Message Types (compatible with protocol.h)
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
    PING_REQUEST = 0x0C,
    PING_RESPONSE = 0x0D,
    ACK = 0x0E,
    ERROR = 0x0F
};

// Enums for Error Codes (compatible with protocol.h)
enum class ErrorCode : uint8_t {
    SUCCESS = 0x00,
    UNKNOWN_MSG_TYPE = 0x01,
    INVALID_FORMAT = 0x02,
    IMAGE_ID_MISMATCH = 0x04,
    PAYLOAD_LENGTH_MISMATCH = 0x05,
    UNSUPPORTED_IMAGE_FORMAT = 0x06,
    OUT_OF_MEMORY = 0x07,
    INTERNAL_ERROR = 0x08,
    INVALID_OPTION_INDEX = 0x09,
    UNSUPPORTED_MESSAGE = 0x0A
};

// SPI Protocol constants
constexpr uint8_t SOF_MARKER = 0x7E;

// Empty structs for protocol compatibility
struct BacklightOn {};
struct BacklightOff {};

namespace DConstant {
    // Keep namespace for backward compatibility
    using ::MediaStatus;
    using ::MediaType;
    using ::ImageFormat;
    using ::ImageResolution;
    using ::FontID;
    using ::Command;
    using ::MessageType;
    using ::ErrorCode;
}

#endif
