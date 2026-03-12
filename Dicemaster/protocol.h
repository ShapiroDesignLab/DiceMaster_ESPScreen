#ifndef DICE_PROTO_H
#define DICE_PROTO_H

#pragma once
// ============================================================================
//  protocol.h - Encoding / decoding helpers for the SPI side-band protocol
//  Updated to match user-supplied enums / macros (MessageType, ErrorCode).
//  Drop this single header into your project; it is self-contained **except**
//  that it relies on the enums/macros you posted being visible beforehand.
// ============================================================================

#include <cstdint>
#include <vector>
#include <string>
#include <variant>
#include <stdexcept>
#include <cstring>

#include "constants.h"

// -----------------------------------------------------------------------
//  Namespace & basic enums
// -----------------------------------------------------------------------
namespace DProtocol {

// ----------------------------------------------------------------------
//  Public enums & constants   (exactly the same numeric values you gave)
// ----------------------------------------------------------------------
// SOF_MARKER is defined in constants.h as 0x7E

// -----------------------------------------------------------------------
//  Structures
// -----------------------------------------------------------------------
struct MessageHeader {
    uint8_t  marker;        // should be SOF_MARKER
    MessageType  type;
    uint8_t  screenId;      // Screen ID (was previously message ID)
    uint16_t length;
};

// -------------------------  Text batch payloads ------------------------
struct TextItem {
    uint16_t x, y;
    uint8_t  font;
    uint16_t color;        // BYTE 5-6: Font Color (2 bytes)
    uint8_t  len;
    char     text[64];      // max 64 chars per line; adjust to taste
};

struct TextBatch {
    uint16_t bgColor;       // BYTE 0-1: BG Color
    uint16_t fontColor;     // BYTE 2-3: Font Color (deprecated - individual texts now have colors)
    uint8_t  itemCount;     // BYTE 4: number of lines
    uint8_t  rotation;      // BYTE 5: rotation (0=0°, 1=90°, 2=180°, 3=270°)
    TextItem items[10];     // Starting at BYTE 6
};

// -------------------------  Image/GIF payloads -------------------------
struct ImageChunk {
    uint8_t  imgId;
    uint8_t  chunkId;
    uint32_t offset;        // 24-bit for image, 32-bit for GIF
    uint16_t length;
    const uint8_t* data;    // pointer into original buffer
};
struct ImageStart {
    uint8_t  imgId;
    uint8_t  fmtRes;        // 4-bit format | 4-bit res
    uint16_t  delayMs;
    uint32_t totalSize;     // 24-bit in packet, 32-bit here
    uint8_t  numChunks;
    uint8_t  rotation;      // 0=0°, 1=90°, 2=180°, 3=270°
    ImageChunk embeddedChunk; // Optional embedded chunk 0
};



struct ImageEnd { uint8_t imgId; };

// -------------------------  Control / status --------------------------
struct PingRequest {};  // Empty payload

struct PingResponse { 
    uint8_t status;      // 0=OK, 1=Warning, 2=Error
    uint8_t len; 
    char text[255]; 
};

struct Ack   { ErrorCode status; };
struct Error { ErrorCode code; uint8_t len; char text[255]; };

// -----------------------------------------------------------------------
//  VIDEO STREAM PAYLOAD STRUCTS  (Messages 0x10 – 0x15)
// -----------------------------------------------------------------------

// MESSAGE 0x10 — VIDEO_STREAM_INIT
struct VideoStreamInitPayload {
    uint8_t  stream_id;
    uint8_t  codec;        // 0 = H.264 Baseline
    uint16_t width;
    uint16_t height;
    uint8_t  fps;
    uint8_t  rotation;     // Rotation enum value
    uint8_t  gop_size;
    uint16_t config_len;   // byte length of codec config (SPS/PPS) that follows
    const uint8_t* config_data; // pointer into original buffer (not wire bytes)
};

// MESSAGE 0x11 — VIDEO_FRAME_START
struct VideoFrameStartPayload {
    uint8_t  stream_id;
    uint8_t  frame_type;   // 0 = I-frame, 1 = P-frame, 2 = B-frame
    uint32_t pts;          // presentation timestamp (ms)
    uint32_t total_size;   // total compressed frame size in bytes
    uint8_t  num_chunks;   // total number of chunks for this frame
};

// MESSAGE 0x12 — VIDEO_FRAME_CHUNK
struct VideoFrameChunkPayload {
    uint8_t  stream_id;
    uint8_t  chunk_index;
    uint16_t chunk_size;
    const uint8_t* chunk_data; // pointer into original buffer
};

// MESSAGE 0x13 — VIDEO_STREAM_END
struct VideoStreamEndPayload {
    uint8_t stream_id;
    uint8_t reason;  // 0 = normal end
};

// MESSAGE 0x14 — VIDEO_FLUSH
struct VideoFlushPayload {
    uint8_t stream_id;
    uint8_t reason;  // 0 = clean flush, 1 = error recovery
};

// MESSAGE 0x15 — SET_ROTATION
struct SetRotationPayload {
    uint8_t target;     // 0 = screen global, 1 = specific stream
    uint8_t stream_id;  // relevant when target == 1
    uint8_t rotation;   // Rotation enum value
};

// -----------------------------------------------------------------------
//  Tagged-union wrapper
// -----------------------------------------------------------------------
enum PayloadTag : uint8_t {
    TAG_NONE,
    TAG_TEXT_BATCH,
    TAG_IMAGE_START,
    TAG_IMAGE_CHUNK,
    TAG_IMAGE_END,
    TAG_BACKLIGHT_ON,
    TAG_BACKLIGHT_OFF,
    TAG_PING_REQUEST,
    TAG_PING_RESPONSE,
    TAG_ACK,
    TAG_ERROR,
    TAG_VIDEO_STREAM_INIT,
    TAG_VIDEO_FRAME_START,
    TAG_VIDEO_FRAME_CHUNK,
    TAG_VIDEO_STREAM_END,
    TAG_VIDEO_FLUSH,
    TAG_SET_ROTATION
};

struct Payload {
    PayloadTag tag;
    union {
        TextBatch              textBatch;
        ImageStart             imageStart;
        ImageChunk             imageChunk;
        ImageEnd               imageEnd;
        PingRequest            pingRequest;
        PingResponse           pingResponse;
        Ack                    ack;
        Error                  error;
        VideoStreamInitPayload videoStreamInit;
        VideoFrameStartPayload videoFrameStart;
        VideoFrameChunkPayload videoFrameChunk;
        VideoStreamEndPayload  videoStreamEnd;
        VideoFlushPayload      videoFlush;
        SetRotationPayload     setRotation;
    } u;
};

// A decoded message: header + payload
struct Message {
    MessageHeader hdr;
    Payload       payload;
};

struct SlaveResponse {
    uint8_t  marker;        // should be SOF_MARKER_REPLY
    uint8_t msgId;          // ID of the message being replied to
    uint32_t errorCode;     // BYTE 2-5: status code (0=OK, 1=Warning, 2=Error)
    uint16_t padding;       // BYTE 6-7: padding (0)
};

// -----------------------------------------------------------------------
//  Internal little helpers (BE)
// -----------------------------------------------------------------------
inline void writeBE(uint8_t* dst, uint32_t val, uint8_t sz)
{
    for(uint8_t i=0;i<sz;++i)
        dst[i] = (uint8_t)(val >> (8*(sz-i-1)));
}

inline uint32_t readBE(const uint8_t* src, uint8_t sz)
{
    uint32_t v = 0;
    for(uint8_t i=0;i<sz;++i) v = (v<<8)|src[i];
    return v;
}

// -----------------------------------------------------------------------
//  HEADER EN/DE-CODE
// -----------------------------------------------------------------------
inline void encodeHeader(uint8_t* out, MessageType t, uint8_t screenId, uint16_t len)
{
    out[0] = ::SOF_MARKER;  // Use global SOF_MARKER from constants.h
    out[1] = static_cast<uint8_t>(t);
    out[2] = screenId;
    writeBE(out+3, len, 2);
}

inline ErrorCode decodeHeader(const uint8_t* buf, size_t sz, MessageHeader& h)
{
    if(sz < 5) return ErrorCode::HEADER_TOO_SHORT;
    if(buf[0] != ::SOF_MARKER) return ErrorCode::INVALID_SOF_MARKER;
    
    // Validate message type
    uint8_t msg_type = buf[1];
    if(msg_type < 0x01 || msg_type > 0x15) return ErrorCode::INVALID_MESSAGE_TYPE;
    
    h.marker = buf[0];
    h.type   = static_cast<MessageType>(msg_type);
    h.screenId = buf[2];
    h.length = static_cast<uint16_t>(readBE(buf+3, 2));
    
    // Validate that we have enough data for the claimed payload length
    if(sz < 5 + h.length) return ErrorCode::HEADER_LENGTH_MISMATCH;
    
    // Check if the k-th bit in the screen ID matches with SCREEN_ID
    // CAN-bus like behavior: check if this message is intended for this screen
    // If SCREEN_ID = 2, we check if bit 2 (0x04) is set in h.screenId
    uint8_t screenBitMask = 1 << ::SCREEN_ID;  // Create mask for k-th bit
    if((h.screenId & screenBitMask) == 0) {
        // The k-th bit is not set, this message is not for this screen
        Serial.println("[SPI] Screen ID mismatch: expected bit " + String(::SCREEN_ID) + 
                      " set in 0x" + String(h.screenId, HEX) + ", mask: 0x" + String(screenBitMask, HEX));
        return ErrorCode::SCREEN_ID_MISMATCH;
    }
    
    return ErrorCode::SUCCESS;
}

// -----------------------------------------------------------------------
//  PAYLOAD ENCODERS
// -----------------------------------------------------------------------

// Text batch encoder
inline size_t encodeTextBatch(uint8_t* out, const TextBatch& tb)
{
    writeBE(out, tb.bgColor, 2);      // BYTE 0-1: BG Color
    writeBE(out + 2, tb.fontColor, 2); // BYTE 2-3: Font Color
    out[4] = tb.itemCount;            // BYTE 4: number of lines
    out[5] = tb.rotation;             // BYTE 5: rotation
    size_t offset = 6;
    
    for(uint8_t i = 0; i < tb.itemCount; ++i) {
        const TextItem& item = tb.items[i];
        writeBE(out + offset, item.x, 2);        // BYTE 0-1: x cursor
        writeBE(out + offset + 2, item.y, 2);    // BYTE 2-3: y cursor
        out[offset + 4] = item.font;             // BYTE 4: font id
        writeBE(out + offset + 5, item.color, 2); // BYTE 5-6: Font Color (2 bytes)
        out[offset + 7] = item.len;              // BYTE 7: text length
        offset += 8;
        
        memcpy(out + offset, item.text, item.len);
        offset += item.len;
    }
    return offset;
}

// Image start encoder
inline size_t encodeImageStart(uint8_t* out, const ImageStart& is)
{
    out[0] = is.imgId;                     // BYTE 0: image ID
    out[1] = is.fmtRes;                    // BYTE 1: 4-bit Format, 4-bit Resolution
    writeBE(out + 2, is.delayMs, 2);      // BYTE 2-3: Delay Time (2 bytes)
    writeBE(out + 4, is.totalSize, 3);    // BYTE 4-6: total image size (3 bytes)
    out[7] = is.numChunks;                 // BYTE 7: num chunks
    out[8] = is.rotation;                  // BYTE 8: rotation
    return 9;
}

// Image chunk encoder
inline size_t encodeImageChunk(uint8_t* out, const ImageChunk& ic)
{
    out[0] = ic.imgId;
    out[1] = ic.chunkId;
    writeBE(out + 2, ic.offset, 3);
    writeBE(out + 5, ic.length, 2);
    memcpy(out + 7, ic.data, ic.length);
    return 7 + ic.length;
}

// Image end encoder
inline size_t encodeImageEnd(uint8_t* out, const ImageEnd& ie)
{
    out[0] = ie.imgId;
    return 1;
}

// Ping request encoder (empty payload)
inline size_t encodePingRequest(uint8_t* out, const PingRequest& pr)
{
    (void)out; (void)pr; // Suppress unused warnings
    return 0;
}

// Ping response encoder
inline size_t encodePingResponse(uint8_t* out, const PingResponse& pr)
{
    out[0] = pr.status;
    out[1] = pr.len;
    memcpy(out + 2, pr.text, pr.len);
    return 2 + pr.len;
}

// Ack encoder
inline size_t encodeAck(uint8_t* out, const Ack& ack)
{
    out[0] = static_cast<uint8_t>(ack.status);
    return 1;
}

// Error encoder
inline size_t encodeError(uint8_t* out, const Error& err)
{
    out[0] = static_cast<uint8_t>(err.code);
    out[1] = err.len;
    memcpy(out + 2, err.text, err.len);
    return 2 + err.len;
}

// -----------------------------------------------------------------------
//  MAIN ENCODE FUNCTION
// -----------------------------------------------------------------------
inline size_t encode(uint8_t* buffer, size_t bufferSize, const Message& msg)
{
    if(bufferSize < 5) return 0; // Need at least header space
    
    // Encode payload first to get its length
    uint8_t* payloadBuffer = buffer + 5;
    size_t maxPayloadSize = bufferSize - 5;
    size_t payloadLen = 0;
    
    switch(msg.payload.tag) {
        case TAG_TEXT_BATCH:
            payloadLen = encodeTextBatch(payloadBuffer, msg.payload.u.textBatch);
            break;
        case TAG_IMAGE_START:
            payloadLen = encodeImageStart(payloadBuffer, msg.payload.u.imageStart);
            break;
        case TAG_IMAGE_CHUNK:
            if(msg.payload.u.imageChunk.length > maxPayloadSize - 7) return 0;
            payloadLen = encodeImageChunk(payloadBuffer, msg.payload.u.imageChunk);
            break;
        case TAG_IMAGE_END:
            payloadLen = encodeImageEnd(payloadBuffer, msg.payload.u.imageEnd);
            break;
        case TAG_PING_REQUEST:
            payloadLen = encodePingRequest(payloadBuffer, msg.payload.u.pingRequest);
            break;
        case TAG_PING_RESPONSE:
            payloadLen = encodePingResponse(payloadBuffer, msg.payload.u.pingResponse);
            break;
        case TAG_BACKLIGHT_ON:
        case TAG_BACKLIGHT_OFF:
            payloadLen = 0; // No payload
            break;
        case TAG_ACK:
            payloadLen = encodeAck(payloadBuffer, msg.payload.u.ack);
            break;
        case TAG_ERROR:
            payloadLen = encodeError(payloadBuffer, msg.payload.u.error);
            break;
        default:
            return 0; // Unknown payload type
    }
    
    if(payloadLen > maxPayloadSize) return 0; // Payload too large
    
    // Encode header
    encodeHeader(buffer, msg.hdr.type, msg.hdr.screenId, static_cast<uint16_t>(payloadLen));
    
    return 5 + payloadLen;
}

// -----------------------------------------------------------------------
//  REPLY ENCODING FUNCTION
// -----------------------------------------------------------------------
inline size_t encode_reply(uint8_t* buffer, size_t bufferSize, uint8_t msgId, uint32_t errorCode)
{
    if(bufferSize < 8) return 0; // Need 8 bytes for SlaveResponse
    
    SlaveResponse response;
    response.marker = ::SOF_MARKER_REPLY;
    response.msgId = msgId;
    response.errorCode = errorCode;
    response.padding = 0;
    
    // Encode the response directly into buffer
    buffer[0] = response.marker;
    buffer[1] = response.msgId;
    writeBE(buffer + 2, response.errorCode, 4);  // 4-byte error code
    writeBE(buffer + 6, response.padding, 2);    // 2-byte padding
    
    // Print the first 8 bytes of the buffer as hex numbers
    Serial.print("Buffer (hex): ");
    for(size_t i = 0; i < 8; ++i) {
        Serial.print("0x");
        if(buffer[i] < 0x10) Serial.print("0"); // Add leading zero for single-digit hex
        Serial.print(buffer[i], HEX);
        if(i < 7) Serial.print(" ");
    }
    Serial.println();
    
    return 8;
}

// -----------------------------------------------------------------------
//  PAYLOAD DECODERS
// -----------------------------------------------------------------------

// Text batch decoder
inline ErrorCode decodeTextBatch(const uint8_t* p, size_t len, TextBatch& tb)
{
    if(len < 6) return ErrorCode::TEXT_PAYLOAD_TOO_SHORT;
    
    tb.bgColor = static_cast<uint16_t>(readBE(p, 2));      // BYTE 0-1: BG Color
    tb.fontColor = static_cast<uint16_t>(readBE(p + 2, 2)); // BYTE 2-3: Font Color
    tb.itemCount = p[4];                                   // BYTE 4: number of lines
    tb.rotation = p[5];                                    // BYTE 5: rotation
    
    if(tb.itemCount > 10) return ErrorCode::TEXT_TOO_MANY_ITEMS;
    if(tb.rotation > 3) return ErrorCode::TEXT_INVALID_ROTATION;
    
    size_t offset = 6;
    Serial.println("[TEXT BATCH] Decoding TextBatch with " + String(tb.itemCount) + " items at " + String(len) + " bytes");
    for(uint8_t i = 0; i < tb.itemCount; ++i) {
        // Serial.println("[TEXT BATCH] Decoding item " + String(i));
        if(offset + 8 > len) return ErrorCode::TEXT_ITEM_HEADER_TOO_SHORT;
        
        TextItem& item = tb.items[i];
        item.x = static_cast<uint16_t>(readBE(p + offset, 2));     // BYTE 0-1: x cursor
        item.y = static_cast<uint16_t>(readBE(p + offset + 2, 2)); // BYTE 2-3: y cursor
        item.font = p[offset + 4];                                 // BYTE 4: font id
        item.color = static_cast<uint16_t>(readBE(p + offset + 5, 2)); // BYTE 5-6: Font Color (2 bytes)
        item.len = p[offset + 7];                                  // BYTE 7: text length
        offset += 8;
        
        if(offset + item.len > len) return ErrorCode::TEXT_ITEM_LENGTH_MISMATCH;
        if(item.len > 64) return ErrorCode::TEXT_PAYLOAD_TRUNCATED;  // Max text length check
        
        memcpy(item.text, p + offset, item.len);
        item.text[item.len] = '\0'; // null terminate
        offset += item.len;
    }
    
    return (offset == len) ? ErrorCode::SUCCESS : ErrorCode::TEXT_LENGTH_CALCULATION_ERROR;
}

// Image start decoder
inline ErrorCode decodeImageStart(const uint8_t* p, size_t len, ImageStart& is)
{
    if(len < 9) return ErrorCode::IMAGE_START_TOO_SHORT;  // Minimum 9 bytes for header
    
    is.imgId = p[0];                               // BYTE 0: image ID
    is.fmtRes = p[1];                              // BYTE 1: 4-bit Format, 4-bit Resolution
    is.delayMs = static_cast<uint16_t>(readBE(p + 2, 2)); // BYTE 2-3: Delay Time (2 bytes)
    is.totalSize = readBE(p + 4, 3);              // BYTE 4-6: total image size (3 bytes)
    is.numChunks = p[7];                          // BYTE 7: num chunks  
    is.rotation = p[8];                           // BYTE 8: rotation
    
    if(is.rotation > 3) return ErrorCode::IMAGE_START_INVALID_ROTATION;
    
    // Validate format (upper 4 bits)
    uint8_t format = (is.fmtRes >> 4) & 0x0F;
    if(format == 0 || format > 3) {
        Serial.println("[SPI ERROR] Invalid image format: " + String(format));
        return ErrorCode::IMAGE_START_INVALID_FORMAT;
    }
    
    // Validate resolution (lower 4 bits)
    uint8_t resolution = is.fmtRes & 0x0F;
    if(resolution == 0 || resolution > 2) return ErrorCode::IMAGE_START_INVALID_RESOLUTION;
    
    // Extract embedded chunk 0 data (starts at BYTE 9)
    if(len > 9) {
        is.embeddedChunk.imgId = is.imgId;
        is.embeddedChunk.chunkId = 0;  // This is chunk 0
        is.embeddedChunk.offset = 0;   // Start at beginning of image
        is.embeddedChunk.length = static_cast<uint16_t>(len - 9);  // Remaining data is chunk 0
        is.embeddedChunk.data = p + 9;  // Pointer to chunk 0 data
        Serial.println("[DECODE] ImageStart with embedded chunk 0: " + String(is.embeddedChunk.length) + " bytes");
    } else {
        is.embeddedChunk.imgId = is.imgId;
        is.embeddedChunk.chunkId = 0;
        is.embeddedChunk.offset = 0;
        is.embeddedChunk.length = 0;
        is.embeddedChunk.data = nullptr;
        Serial.println("[DECODE] ImageStart with no embedded chunk data");
    }
    
    return ErrorCode::SUCCESS;
}

// Image chunk decoder
inline ErrorCode decodeImageChunk(const uint8_t* p, size_t len, ImageChunk& ic)
{
    if(len < 7) return ErrorCode::IMAGE_CHUNK_TOO_SHORT;
    
    ic.imgId = p[0];
    ic.chunkId = p[1];
    ic.offset = readBE(p + 2, 3); // 24-bit for image
    ic.length = static_cast<uint16_t>(readBE(p + 5, 2));
    
    if(ic.length == 0) return ErrorCode::IMAGE_CHUNK_INVALID_LENGTH;
    if(len < 7 + ic.length) return ErrorCode::IMAGE_CHUNK_DATA_TRUNCATED;
    
    ic.data = p + 7;
    return ErrorCode::SUCCESS;
}

// Image end decoder
inline ErrorCode decodeImageEnd(const uint8_t* p, size_t len, ImageEnd& ie)
{
    if(len < 1) return ErrorCode::IMAGE_END_TOO_SHORT;
    ie.imgId = p[0];
    return ErrorCode::SUCCESS;
}

// Ping request decoder (empty payload)
inline ErrorCode decodePingRequest(const uint8_t* p, size_t len, PingRequest& pr)
{
    (void)p; (void)pr; // Suppress unused warnings
    return (len == 0) ? ErrorCode::SUCCESS : ErrorCode::PING_REQUEST_NOT_EMPTY;
}

// Ping response decoder
inline ErrorCode decodePingResponse(const uint8_t* p, size_t len, PingResponse& pr)
{
    if(len < 2) return ErrorCode::PING_RESPONSE_TOO_SHORT;
    
    pr.status = p[0];
    pr.len = p[1];
    
    if(len < 2 + pr.len) return ErrorCode::PING_RESPONSE_TEXT_TRUNCATED;
    if(pr.len > 255) return ErrorCode::PING_RESPONSE_TEXT_TRUNCATED;  // Sanity check
    
    memcpy(pr.text, p + 2, pr.len);
    pr.text[pr.len] = '\0'; // null terminate
    return ErrorCode::SUCCESS;
}

// Ack decoder
inline ErrorCode decodeAck(const uint8_t* p, size_t len, Ack& ack)
{
    if(len < 1) return ErrorCode::ACK_TOO_SHORT;
    ack.status = static_cast<ErrorCode>(p[0]);
    return ErrorCode::SUCCESS;
}

// Error decoder
inline ErrorCode decodeError(const uint8_t* p, size_t len, Error& err)
{
    if(len < 2) return ErrorCode::ERROR_TOO_SHORT;
    
    err.code = static_cast<ErrorCode>(p[0]);
    err.len = p[1];
    
    if(len < 2 + err.len) return ErrorCode::ERROR_TEXT_TRUNCATED;
    if(err.len > 255) return ErrorCode::ERROR_TEXT_TRUNCATED;  // Sanity check
    
    memcpy(err.text, p + 2, err.len);
    err.text[err.len] = '\0'; // null terminate
    return ErrorCode::SUCCESS;
}

// -----------------------------------------------------------------------
//  VIDEO STREAM DECODE HELPERS
// -----------------------------------------------------------------------

// VIDEO_STREAM_INIT: stream_id(1) codec(1) width(2) height(2) fps(1) rotation(1) gop_size(1) config_len(2) [config_data]
inline ErrorCode decodeVideoStreamInit(const uint8_t* p, size_t len, VideoStreamInitPayload& v) {
    if (len < 11) return ErrorCode::UNSUPPORTED_MESSAGE;
    v.stream_id  = p[0];
    v.codec      = p[1];
    v.width      = static_cast<uint16_t>(readBE(p + 2, 2));
    v.height     = static_cast<uint16_t>(readBE(p + 4, 2));
    v.fps        = p[6];
    v.rotation   = p[7];
    v.gop_size   = p[8];
    v.config_len = static_cast<uint16_t>(readBE(p + 9, 2));
    v.config_data = (v.config_len > 0 && len >= 11u + v.config_len) ? p + 11 : nullptr;
    return ErrorCode::SUCCESS;
}

// VIDEO_FRAME_START: stream_id(1) frame_type(1) pts(4) total_size(4) num_chunks(1)
inline ErrorCode decodeVideoFrameStart(const uint8_t* p, size_t len, VideoFrameStartPayload& v) {
    if (len < 11) return ErrorCode::UNSUPPORTED_MESSAGE;
    v.stream_id   = p[0];
    v.frame_type  = p[1];
    v.pts         = readBE(p + 2, 4);
    v.total_size  = readBE(p + 6, 4);
    v.num_chunks  = p[10];
    return ErrorCode::SUCCESS;
}

// VIDEO_FRAME_CHUNK: stream_id(1) chunk_index(1) chunk_size(2) [data]
inline ErrorCode decodeVideoFrameChunk(const uint8_t* p, size_t len, VideoFrameChunkPayload& v) {
    if (len < 4) return ErrorCode::UNSUPPORTED_MESSAGE;
    v.stream_id   = p[0];
    v.chunk_index = p[1];
    v.chunk_size  = static_cast<uint16_t>(readBE(p + 2, 2));
    v.chunk_data  = (v.chunk_size > 0 && len >= 4u + v.chunk_size) ? p + 4 : nullptr;
    return ErrorCode::SUCCESS;
}

// VIDEO_STREAM_END: stream_id(1) reason(1)
inline ErrorCode decodeVideoStreamEnd(const uint8_t* p, size_t len, VideoStreamEndPayload& v) {
    if (len < 2) return ErrorCode::UNSUPPORTED_MESSAGE;
    v.stream_id = p[0];
    v.reason    = p[1];
    return ErrorCode::SUCCESS;
}

// VIDEO_FLUSH: stream_id(1) reason(1)
inline ErrorCode decodeVideoFlush(const uint8_t* p, size_t len, VideoFlushPayload& v) {
    if (len < 2) return ErrorCode::UNSUPPORTED_MESSAGE;
    v.stream_id = p[0];
    v.reason    = p[1];
    return ErrorCode::SUCCESS;
}

// SET_ROTATION: target(1) stream_id(1) rotation(1)
inline ErrorCode decodeSetRotation(const uint8_t* p, size_t len, SetRotationPayload& v) {
    if (len < 3) return ErrorCode::UNSUPPORTED_MESSAGE;
    v.target    = p[0];
    v.stream_id = p[1];
    v.rotation  = p[2];
    return ErrorCode::SUCCESS;
}

// -----------------------------------------------------------------------
//  MAIN DECODE FUNCTION
// -----------------------------------------------------------------------
inline ErrorCode decode(const uint8_t* buffer, size_t bufferSize, Message& msg)
{
    // Decode header
    ErrorCode result = decodeHeader(buffer, bufferSize, msg.hdr);
    if(result != ErrorCode::SUCCESS) return result;
    
    // Header validation already checks this, but double-check for safety
    if(bufferSize < 5 + msg.hdr.length) return ErrorCode::HEADER_LENGTH_MISMATCH;
    
    const uint8_t* payload = buffer + 5;
    size_t payloadLen = msg.hdr.length;
    
    // Decode payload based on message type
    switch(msg.hdr.type) {
        case MessageType::TEXT_BATCH:
            msg.payload.tag = TAG_TEXT_BATCH;
            return decodeTextBatch(payload, payloadLen, msg.payload.u.textBatch);
            
        case MessageType::IMAGE_TRANSFER_START:
            msg.payload.tag = TAG_IMAGE_START;
            return decodeImageStart(payload, payloadLen, msg.payload.u.imageStart);
            
        case MessageType::IMAGE_CHUNK:
            msg.payload.tag = TAG_IMAGE_CHUNK;
            return decodeImageChunk(payload, payloadLen, msg.payload.u.imageChunk);
            
        case MessageType::IMAGE_TRANSFER_END:
            msg.payload.tag = TAG_IMAGE_END;
            return decodeImageEnd(payload, payloadLen, msg.payload.u.imageEnd);
            
        case MessageType::BACKLIGHT_ON:
            msg.payload.tag = TAG_BACKLIGHT_ON;
            return ErrorCode::SUCCESS;
            
        case MessageType::BACKLIGHT_OFF:
            msg.payload.tag = TAG_BACKLIGHT_OFF;
            return ErrorCode::SUCCESS;
            
        case MessageType::PING_REQUEST:
            msg.payload.tag = TAG_PING_REQUEST;
            return decodePingRequest(payload, payloadLen, msg.payload.u.pingRequest);
            
        case MessageType::PING_RESPONSE:
            msg.payload.tag = TAG_PING_RESPONSE;
            return decodePingResponse(payload, payloadLen, msg.payload.u.pingResponse);
            
        case MessageType::ACK:
            msg.payload.tag = TAG_ACK;
            return decodeAck(payload, payloadLen, msg.payload.u.ack);
            
        case MessageType::ERROR:
            msg.payload.tag = TAG_ERROR;
            return decodeError(payload, payloadLen, msg.payload.u.error);

        case MessageType::VIDEO_STREAM_INIT:
            msg.payload.tag = TAG_VIDEO_STREAM_INIT;
            return decodeVideoStreamInit(payload, payloadLen, msg.payload.u.videoStreamInit);

        case MessageType::VIDEO_FRAME_START:
            msg.payload.tag = TAG_VIDEO_FRAME_START;
            return decodeVideoFrameStart(payload, payloadLen, msg.payload.u.videoFrameStart);

        case MessageType::VIDEO_FRAME_CHUNK:
            msg.payload.tag = TAG_VIDEO_FRAME_CHUNK;
            return decodeVideoFrameChunk(payload, payloadLen, msg.payload.u.videoFrameChunk);

        case MessageType::VIDEO_STREAM_END:
            msg.payload.tag = TAG_VIDEO_STREAM_END;
            return decodeVideoStreamEnd(payload, payloadLen, msg.payload.u.videoStreamEnd);

        case MessageType::VIDEO_FLUSH:
            msg.payload.tag = TAG_VIDEO_FLUSH;
            return decodeVideoFlush(payload, payloadLen, msg.payload.u.videoFlush);

        case MessageType::SET_ROTATION:
            msg.payload.tag = TAG_SET_ROTATION;
            return decodeSetRotation(payload, payloadLen, msg.payload.u.setRotation);

        default:
            return ErrorCode::UNSUPPORTED_MESSAGE;
    }
}

} // namespace DProtocol

// Export the namespace for global use
using namespace DProtocol;

#endif // DICE_PROTO_H