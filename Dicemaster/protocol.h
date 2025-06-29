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
    uint8_t  id;
    uint16_t length;
};

// -------------------------  Text batch payloads ------------------------
struct TextItem {
    uint16_t x, y;
    uint8_t  font;
    uint8_t  color;
    uint8_t  len;
    char     text[64];      // max 64 chars per line; adjust to taste
};

struct TextBatch {
    uint8_t  itemCount;
    uint8_t  rotation;      // 0=0°, 1=90°, 2=180°, 3=270°
    TextItem items[10];     // max 10 lines; adjust to taste
};

// -------------------------  Image/GIF payloads -------------------------
struct ImageStart {
    uint8_t  imgId;
    uint8_t  fmtRes;        // 4-bit format | 4-bit res
    uint8_t  delayMs;
    uint32_t totalSize;     // 24-bit in packet, 32-bit here
    uint8_t  numChunks;
    uint8_t  rotation;      // 0=0°, 1=90°, 2=180°, 3=270°
};

struct ImageChunk {
    uint8_t  imgId;
    uint8_t  chunkId;
    uint32_t offset;        // 24-bit for image, 32-bit for GIF
    uint16_t length;
    const uint8_t* data;    // pointer into original buffer
};

struct ImageEnd { uint8_t imgId; };

// -------------------------  Option list payloads ----------------------
struct OptionEntry {
    uint8_t  selected;
    uint16_t x, y;
    uint8_t  len;
    char     text[255];
};

struct OptionList {
    uint8_t entryCount;
    OptionEntry entries[12];   // adjust size as needed
};

struct OptionSelectionUpdate { uint8_t index; };

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
//  Tagged-union wrapper
// -----------------------------------------------------------------------
enum PayloadTag : uint8_t {
    TAG_NONE,
    TAG_TEXT_BATCH,
    TAG_IMAGE_START,
    TAG_IMAGE_CHUNK,
    TAG_IMAGE_END,
    TAG_OPTION_LIST,
    TAG_OPTION_UPDATE,
    TAG_BACKLIGHT_ON,
    TAG_BACKLIGHT_OFF,
    TAG_PING_REQUEST,
    TAG_PING_RESPONSE,
    TAG_ACK,
    TAG_ERROR
};

struct Payload {
    PayloadTag tag;
    union {
        TextBatch              textBatch;
        ImageStart             imageStart;
        ImageChunk             imageChunk;
        ImageEnd               imageEnd;
        OptionList             optionList;
        OptionSelectionUpdate  optionUpdate;
        PingRequest            pingRequest;
        PingResponse           pingResponse;
        Ack                    ack;
        Error                  error;
    } u;
};

// A decoded message: header + payload
struct Message {
    MessageHeader hdr;
    Payload       payload;
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
inline void encodeHeader(uint8_t* out, MessageType t, uint8_t id, uint16_t len)
{
    out[0] = ::SOF_MARKER;  // Use global SOF_MARKER from constants.h
    out[1] = static_cast<uint8_t>(t);
    out[2] = id;
    writeBE(out+3, len, 2);
}

inline ErrorCode decodeHeader(const uint8_t* buf, size_t sz, MessageHeader& h)
{
    if(sz < 5) return ErrorCode::INVALID_FORMAT;
    if(buf[0] != ::SOF_MARKER) return ErrorCode::INVALID_FORMAT;  // Use global SOF_MARKER
    h.marker = buf[0];
    h.type   = static_cast<MessageType>(buf[1]);
    h.id     = buf[2];
    h.length = static_cast<uint16_t>(readBE(buf+3, 2));
    return ErrorCode::SUCCESS;
}

// -----------------------------------------------------------------------
//  PAYLOAD ENCODERS
// -----------------------------------------------------------------------

// Text batch encoder
inline size_t encodeTextBatch(uint8_t* out, const TextBatch& tb)
{
    out[0] = tb.itemCount;
    out[1] = tb.rotation;
    size_t offset = 2;
    
    for(uint8_t i = 0; i < tb.itemCount; ++i) {
        const TextItem& item = tb.items[i];
        writeBE(out + offset, item.x, 2);
        writeBE(out + offset + 2, item.y, 2);
        out[offset + 4] = item.font;
        out[offset + 5] = item.color;
        out[offset + 6] = item.len;
        offset += 7;
        
        memcpy(out + offset, item.text, item.len);
        offset += item.len;
    }
    return offset;
}

// Image start encoder
inline size_t encodeImageStart(uint8_t* out, const ImageStart& is)
{
    out[0] = is.imgId;
    out[1] = is.fmtRes;
    out[2] = is.delayMs;
    writeBE(out + 3, is.totalSize, 3);
    out[6] = is.numChunks;
    out[7] = is.rotation;
    return 8;
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

// Option list encoder
inline size_t encodeOptionList(uint8_t* out, const OptionList& ol)
{
    out[0] = ol.entryCount;
    size_t offset = 1;
    
    for(uint8_t i = 0; i < ol.entryCount; ++i) {
        const OptionEntry& entry = ol.entries[i];
        out[offset] = entry.selected;
        writeBE(out + offset + 1, entry.x, 2);
        writeBE(out + offset + 3, entry.y, 2);
        out[offset + 5] = entry.len;
        offset += 6;
        
        memcpy(out + offset, entry.text, entry.len);
        offset += entry.len;
    }
    return offset;
}

// Option selection update encoder
inline size_t encodeOptionUpdate(uint8_t* out, const OptionSelectionUpdate& osu)
{
    out[0] = osu.index;
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
        case TAG_OPTION_LIST:
            payloadLen = encodeOptionList(payloadBuffer, msg.payload.u.optionList);
            break;
        case TAG_OPTION_UPDATE:
            payloadLen = encodeOptionUpdate(payloadBuffer, msg.payload.u.optionUpdate);
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
    encodeHeader(buffer, msg.hdr.type, msg.hdr.id, static_cast<uint16_t>(payloadLen));
    
    return 5 + payloadLen;
}

// -----------------------------------------------------------------------
//  PAYLOAD DECODERS
// -----------------------------------------------------------------------

// Text batch decoder
inline ErrorCode decodeTextBatch(const uint8_t* p, size_t len, TextBatch& tb)
{
    if(len < 2) return ErrorCode::INVALID_FORMAT;
    tb.itemCount = p[0];
    tb.rotation = p[1];
    if(tb.itemCount > 10) return ErrorCode::OUT_OF_MEMORY;
    if(tb.rotation > 3) return ErrorCode::INVALID_FORMAT;
    
    size_t offset = 2;
    for(uint8_t i = 0; i < tb.itemCount; ++i) {
        if(offset + 7 > len) return ErrorCode::INVALID_FORMAT;
        TextItem& item = tb.items[i];
        item.x = static_cast<uint16_t>(readBE(p + offset, 2));
        item.y = static_cast<uint16_t>(readBE(p + offset + 2, 2));
        item.font = p[offset + 4];
        item.color = p[offset + 5];
        item.len = p[offset + 6];
        offset += 7;
        
        if(offset + item.len > len) return ErrorCode::INVALID_FORMAT;
        memcpy(item.text, p + offset, item.len);
        item.text[item.len] = '\0'; // null terminate
        offset += item.len;
    }
    return (offset == len) ? ErrorCode::SUCCESS : ErrorCode::INVALID_FORMAT;
}

// Image start decoder
inline ErrorCode decodeImageStart(const uint8_t* p, size_t len, ImageStart& is)
{
    if(len < 8) return ErrorCode::INVALID_FORMAT;
    is.imgId = p[0];
    is.fmtRes = p[1];
    is.delayMs = p[2];
    is.totalSize = readBE(p + 3, 3); // 24-bit
    is.numChunks = p[6];
    is.rotation = p[7];
    if(is.rotation > 3) return ErrorCode::INVALID_FORMAT;
    return ErrorCode::SUCCESS;
}

// Image chunk decoder
inline ErrorCode decodeImageChunk(const uint8_t* p, size_t len, ImageChunk& ic)
{
    if(len < 7) return ErrorCode::INVALID_FORMAT;
    ic.imgId = p[0];
    ic.chunkId = p[1];
    ic.offset = readBE(p + 2, 3); // 24-bit for image
    ic.length = static_cast<uint16_t>(readBE(p + 5, 2));
    if(len < 7 + ic.length) return ErrorCode::INVALID_FORMAT;
    ic.data = p + 7;
    return ErrorCode::SUCCESS;
}

// Image end decoder
inline ErrorCode decodeImageEnd(const uint8_t* p, size_t len, ImageEnd& ie)
{
    if(len < 1) return ErrorCode::INVALID_FORMAT;
    ie.imgId = p[0];
    return ErrorCode::SUCCESS;
}

// Option list decoder
inline ErrorCode decodeOptionList(const uint8_t* p, size_t len, OptionList& ol)
{
    if(len < 1) return ErrorCode::INVALID_FORMAT;
    ol.entryCount = p[0];
    if(ol.entryCount > 12) return ErrorCode::OUT_OF_MEMORY;
    
    size_t offset = 1;
    for(uint8_t i = 0; i < ol.entryCount; ++i) {
        if(offset + 6 > len) return ErrorCode::INVALID_FORMAT;
        OptionEntry& entry = ol.entries[i];
        entry.selected = p[offset];
        entry.x = static_cast<uint16_t>(readBE(p + offset + 1, 2));
        entry.y = static_cast<uint16_t>(readBE(p + offset + 3, 2));
        entry.len = p[offset + 5];
        offset += 6;
        
        if(offset + entry.len > len) return ErrorCode::INVALID_FORMAT;
        memcpy(entry.text, p + offset, entry.len);
        entry.text[entry.len] = '\0'; // null terminate
        offset += entry.len;
    }
    return (offset == len) ? ErrorCode::SUCCESS : ErrorCode::INVALID_FORMAT;
}

// Option selection update decoder
inline ErrorCode decodeOptionUpdate(const uint8_t* p, size_t len, OptionSelectionUpdate& osu)
{
    if(len < 1) return ErrorCode::INVALID_FORMAT;
    osu.index = p[0];
    return ErrorCode::SUCCESS;
}

// Ping request decoder (empty payload)
inline ErrorCode decodePingRequest(const uint8_t* p, size_t len, PingRequest& pr)
{
    (void)p; (void)pr; // Suppress unused warnings
    return (len == 0) ? ErrorCode::SUCCESS : ErrorCode::INVALID_FORMAT;
}

// Ping response decoder
inline ErrorCode decodePingResponse(const uint8_t* p, size_t len, PingResponse& pr)
{
    if(len < 2) return ErrorCode::INVALID_FORMAT;
    pr.status = p[0];
    pr.len = p[1];
    if(len < 2 + pr.len) return ErrorCode::INVALID_FORMAT;
    memcpy(pr.text, p + 2, pr.len);
    pr.text[pr.len] = '\0'; // null terminate
    return ErrorCode::SUCCESS;
}

// Ack decoder
inline ErrorCode decodeAck(const uint8_t* p, size_t len, Ack& ack)
{
    if(len < 1) return ErrorCode::INVALID_FORMAT;
    ack.status = static_cast<ErrorCode>(p[0]);
    return ErrorCode::SUCCESS;
}

// Error decoder
inline ErrorCode decodeError(const uint8_t* p, size_t len, Error& err)
{
    if(len < 2) return ErrorCode::INVALID_FORMAT;
    err.code = static_cast<ErrorCode>(p[0]);
    err.len = p[1];
    if(len < 2 + err.len) return ErrorCode::INVALID_FORMAT;
    memcpy(err.text, p + 2, err.len);
    err.text[err.len] = '\0'; // null terminate
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
    
    if(bufferSize < 5 + msg.hdr.length) return ErrorCode::INVALID_FORMAT;
    
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
            
        case MessageType::OPTION_LIST:
            msg.payload.tag = TAG_OPTION_LIST;
            return decodeOptionList(payload, payloadLen, msg.payload.u.optionList);
            
        case MessageType::OPTION_SELECTION_UPDATE:
            msg.payload.tag = TAG_OPTION_UPDATE;
            return decodeOptionUpdate(payload, payloadLen, msg.payload.u.optionUpdate);
            
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
            
        default:
            return ErrorCode::UNSUPPORTED_MESSAGE;
    }
}

} // namespace DProtocol

// Export the namespace for global use
using namespace DProtocol;

#endif // DICE_PROTO_H
