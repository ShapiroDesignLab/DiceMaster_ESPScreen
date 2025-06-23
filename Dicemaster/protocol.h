#ifndef DICE_PROTO_H
#define DICE_PROTO_H

#pragma once
// ============================================================================
//  protocol.h  –  Encoding / decoding helpers for the SPI side‑band protocol
//  Updated to match user‑supplied enums / macros (MessageType, ErrorCode …).
//  Drop this single header into your project; it is self‑contained **except**
//  that it relies on the enums/macros you posted being visible beforehand.
// ============================================================================

#include <cstdint>
#include <vector>
#include <string>
#include <variant>
#include <stdexcept>
#include <cstring>

#include "constants.h"

// ───────────────────────  Namespace & basic enums  ─────────────────────
namespace DProtocol {

// ----------------------------------------------------------------------
//  Public enums & constants   (exactly the same numeric values you gave)
// ----------------------------------------------------------------------
enum { SOF_MARKER = 0x7E };

// ─────────────────────────────  Structures  ────────────────────────────
struct MessageHeader {
    uint8_t  sof;          // always SOF_MARKER
    MessageType type;
    uint8_t  id;
    uint16_t payloadLen;   // big-endian
};

// -------------------------  Text batch payload -------------------------
struct TextItem {
    uint16_t x, y;
    uint8_t  fontId;
    uint8_t  len;          // length of text[]
    uint8_t  text[255];    // UTF-8 (len bytes used)
};

struct TextBatch {
    uint16_t bgColor;
    uint16_t fontColor;
    uint8_t  itemCount;
    TextItem items[10];     // max 10 lines; adjust to taste
};

// -------------------------  Image/GIF payloads -------------------------
struct ImageStart {
    uint8_t  imgId;
    uint8_t  fmtRes;        // 4-bit format | 4-bit res
    uint8_t  delayMs;
    uint32_t totalSize;     // 24-bit in packet, 32-bit here
    uint8_t  numChunks;
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
struct Ack   { ErrorCode status; };
struct Error { ErrorCode code; uint8_t len; char text[255]; };

// ────────────────────────  Tagged-union wrapper  ───────────────────────
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
        Ack                    ack;
        Error                  error;
    } u;
};

// A decoded message: header + payload
struct Message {
    MessageHeader hdr;
    Payload       payload;
};

// ──────────────────  Internal little helpers (BE)  ────────────────────
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

// ─────────────────────────  HEADER EN/DE-CODE  ────────────────────────
inline void encodeHeader(uint8_t* out, MessageType t, uint8_t id, uint16_t len)
{
    out[0] = SOF_MARKER;
    out[1] = t;
    out[2] = id;
    writeBE(out+3, len, 2);
}

inline ErrorCode decodeHeader(const uint8_t* buf, size_t sz, MessageHeader& h)
{
    if(sz < 5)            return DConstant::ErrorCode::INVALID_FORMAT;
    if(buf[0] != SOF_MARKER) return DConstant::ErrorCode::INVALID_FORMAT;
    h.sof = buf[0];
    h.type = static_cast<MessageType>(buf[1]);
    h.id   = buf[2];
    h.payloadLen = (uint16_t)((buf[3]<<8)|buf[4]);
    if(h.payloadLen + 5 != sz) return PAYLOAD_LENGTH_MISMATCH;
    return SUCCESS;
}

// ─────────────────────  PER-TYPE ENCODERS (examples)  ──────────────────
// Only a couple shown; add the rest as needed.
// All return std::vector<uint8_t> ready to send.

inline std::vector<uint8_t> encode(const TextBatch& tb, uint8_t msgId)
{
    std::vector<uint8_t> p;
    p.reserve(5 + tb.itemCount*16);

    // group header
    p.push_back(uint8_t(tb.bgColor>>8)); p.push_back(uint8_t(tb.bgColor));
    p.push_back(uint8_t(tb.fontColor>>8)); p.push_back(uint8_t(tb.fontColor));
    p.push_back(tb.itemCount);

    // items
    for(uint8_t i=0;i<tb.itemCount;++i){
        const TextItem& it = tb.items[i];
        p.push_back(uint8_t(it.x>>8)); p.push_back(uint8_t(it.x));
        p.push_back(uint8_t(it.y>>8)); p.push_back(uint8_t(it.y));
        p.push_back(it.fontId);
        p.push_back(it.len);
        p.insert(p.end(), it.text, it.text+it.len);
    }

    std::vector<uint8_t> out(5);                // header placeholder
    encodeHeader(out.data(), TEXT_BATCH, msgId, p.size());
    out.insert(out.end(), p.begin(), p.end());
    return out;
}

// (Add encode() overloads for other structs analogously)

// ─────────────────────  PER-TYPE DECODERS (examples)  ──────────────────
inline ErrorCode decodeTextBatch(const uint8_t* p, size_t len, TextBatch& tb)
{
    if(len < 5) return INVALID_FORMAT;
    tb.bgColor   = readBE(p,2); tb.fontColor = readBE(p+2,2);
    tb.itemCount = p[4]; p+=5; len-=5;
    if(tb.itemCount > 10) return OUT_OF_MEMORY;

    for(uint8_t i=0;i<tb.itemCount;++i){
        if(len < 6) return INVALID_FORMAT;
        TextItem& it = tb.items[i];
        it.x = readBE(p,2); it.y = readBE(p+2,2);
        it.fontId = p[4]; it.len = p[5];
        p+=6; len-=6;
        if(len < it.len) return INVALID_FORMAT;
        memcpy(it.text, p, it.len);
        p+=it.len; len-=it.len;
    }
    return (len==0) ? SUCCESS : INVALID_FORMAT;
}

// (Add decodeImageStart, decodeImageChunk… similarly)


// ───────────────────────────────────────── IMAGE helpers (JPEG + GIF)
inline uint8_t packFmtRes(ImageFormat f, ImageResolution r){
    return (static_cast<uint8_t>(f)<<4)|(static_cast<uint8_t>(r)&0x0F);
} 
inline void unpackFmtRes(uint8_t b, ImageFormat& f, ImageResolution& r){
    f=static_cast<ImageFormat>(b>>4); r=static_cast<ImageResolution>(b&0x0F);
} 

inline std::vector<uint8_t> encode(const ImageStart& st, uint8_t id, MessageType t)
{
    std::vector<uint8_t> pl{ st.imgId, packFmtRes(st.fmt,st.res), st.delay };
    writeBE(pl, st.total, 3); pl.push_back(st.chunks);
    auto hdr = encodeHeader(t, id, pl.size());
    hdr.insert(hdr.end(), pl.begin(), pl.end());
    return hdr;
}
inline ImageStart decodeImageStart(const uint8_t* p,size_t l){
    if(l!=7) 
        throw std::runtime_error("IS 7");
    ImageStart s; 
    s.imgId=p[0];unpackFmtRes(p[1],s.fmt,s.res);
    s.delay=p[2];
    s.total=readBE(p+3,3);
    s.chunks=p[6];
    return s;
}

inline std::vector<uint8_t> encode(const ImageChunk& ck, uint8_t id, MessageType t)
{
    bool gif = (t==MessageType::GIF_FRAME); size_t offBytes = gif?4:3;
    std::vector<uint8_t> pl{ ck.imgId, ck.chunkId }; writeBE(pl, ck.offset, offBytes);
    if (ck.data.size()>0xFFFF) throw std::runtime_error("chunk>65535");
    writeBE(pl, ck.data.size(),2); pl.insert(pl.end(), ck.data.begin(), ck.data.end());
    auto hdr = encodeHeader(t,id,pl.size()); hdr.insert(hdr.end(),pl.begin(),pl.end()); return hdr;
}
inline ImageChunk decodeImageChunk(const uint8_t* p,size_t l,bool gif){size_t offB=gif?4:3;size_t meta=2+offB+2;if(l<meta)throw std::runtime_error("IC short");ImageChunk c; c.imgId=p[0];c.chunkId=p[1];c.offset=readBE(p+2,offB);uint16_t len=readBE(p+2+offB,2);size_t ds=meta;if(ds+len!=l)throw std::runtime_error("IC len mismatch");c.data.assign(p+ds,p+ds+len);return c;}

inline std::vector<uint8_t> encode(const ImageEnd& e,uint8_t id,MessageType t){std::vector<uint8_t> pl{e.imgId};auto hdr=encodeHeader(t,id,pl.size());hdr.insert(hdr.end(),pl.begin(),pl.end());return hdr;}
inline ImageEnd decodeImageEnd(const uint8_t* p,size_t l){if(l!=1)throw std::runtime_error("IE 1");return ImageEnd{p[0]};}

// ───────────────────────────────────────── OPTION helpers
inline std::vector<uint8_t> encode(const OptionList& ol,uint8_t id)
{
    std::vector<uint8_t> pl{ static_cast<uint8_t>(ol.entries.size()) };
    for(const auto& e:ol.entries){pl.push_back(e.sel?1:0);writeBE(pl,e.x,2);writeBE(pl,e.y,2);if(e.txt.size()>255)throw std::runtime_error("opt len");pl.push_back(static_cast<uint8_t>(e.txt.size()));pl.insert(pl.end(),e.txt.begin(),e.txt.end());}
    auto hdr=encodeHeader(MessageType::OPTION_LIST,id,pl.size());hdr.insert(hdr.end(),pl.begin(),pl.end());return hdr;
}
inline OptionList decodeOptionList(const uint8_t* p,size_t l){if(l<1)throw std::runtime_error("OL empty");uint8_t c=p[0];size_t off=1;OptionList ol;while(c--){if(off+6>l)throw std::runtime_error("OL meta");OptionEntry e; e.sel=p[off]!=0; e.x=readBE(p+off+1,2); e.y=readBE(p+off+3,2);uint8_t tl=p[off+5]; off+=6; if(off+tl>l)throw std::runtime_error("OL txt"); e.txt.assign(reinterpret_cast<const char*>(p+off),tl); off+=tl; ol.entries.push_back(std::move(e));} if(off!=l)throw std::runtime_error("OL leftover"); return ol;}

inline std::vector<uint8_t> encode(const OptionSelectionUpdate& u,uint8_t id){std::vector<uint8_t> pl{u.index};auto hdr=encodeHeader(MessageType::OPTION_SELECTION_UPDATE,id,pl.size());hdr.insert(hdr.end(),pl.begin(),pl.end());return hdr;}
inline OptionSelectionUpdate decodeOptionSelectionUpdate(const uint8_t* p,size_t l){if(l!=1)throw std::runtime_error("OSU");return OptionSelectionUpdate{p[0]};}

// ───────────────────────────────────────── BACKLIGHT ON / OFF (zero payload)
inline std::vector<uint8_t> encode(const BacklightOn&,uint8_t id){auto hdr=encodeHeader(MessageType::BACKLIGHT_ON,id,0);return hdr;}
inline std::vector<uint8_t> encode(const BacklightOff&,uint8_t id){auto hdr=encodeHeader(MessageType::BACKLIGHT_OFF,id,0);return hdr;}

// ───────────────────────────────────────── ACK / ERROR
inline std::vector<uint8_t> encode(const Ack& a,uint8_t id){std::vector<uint8_t> pl{static_cast<uint8_t>(a.status)};auto hdr=encodeHeader(MessageType::ACK,id,pl.size());hdr.insert(hdr.end(),pl.begin(),pl.end());return hdr;}
inline Ack decodeAck(const uint8_t* p,size_t l){if(l!=1)throw std::runtime_error("ACK 1");return Ack{static_cast<ErrorCode>(p[0])};}

inline std::vector<uint8_t> encode(const Error& e,uint8_t id){if(e.desc.size()>255)throw std::runtime_error("err txt>255");std::vector<uint8_t> pl{static_cast<uint8_t>(e.code),static_cast<uint8_t>(e.desc.size())};pl.insert(pl.end(),e.desc.begin(),e.desc.end());auto hdr=encodeHeader(MessageType::ERROR,id,pl.size());hdr.insert(hdr.end(),pl.begin(),pl.end());return hdr;}
inline Error decodeError(const uint8_t* p,size_t l){if(l<2)throw std::runtime_error("ERR short");uint8_t tl=p[1];if(2+tl!=l)throw std::runtime_error("ERR len");return Error{static_cast<ErrorCode>(p[0]),std::string(reinterpret_cast<const char*>(p+2),tl)};}


// ─────────────────────  HIGH-LEVEL DISPATCH DECODER  ───────────────────
// ─────────────────────  HIGH-LEVEL DISPATCH DECODER  ───────────────────
inline ErrorCode decodeMessage(const uint8_t* buf, size_t sz, Message& msg)
{
    ErrorCode ec = decodeHeader(buf, sz, msg.hdr);
    if (ec != SUCCESS) return ec;

    const uint8_t* p   = buf + 5;          // start of payload
    size_t         len = msg.hdr.payloadLen;
    msg.payload.tag    = TAG_NONE;         // clear

    switch (msg.hdr.type)
    {
    // ─────────────── TEXT_BATCH ───────────────
    case MessageType::TEXT_BATCH:
        msg.payload.tag = TAG_TEXT_BATCH;
        return decodeTextBatch(p, len, msg.payload.u.textBatch);

    // ───────────── IMAGE_TRANSFER_START ───────
    case MessageType::IMAGE_TRANSFER_START:
    case MessageType::GIF_TRANSFER_START:
        if (len != 7) return INVALID_FORMAT;
        msg.payload.tag = TAG_IMAGE_START;
        {
            ImageStart& s  = msg.payload.u.imageStart;
            s.imgId        = p[0];
            s.fmtRes       = p[1];
            s.delayMs      = p[2];
            s.totalSize    = (uint32_t(p[3]) << 16) |
                             (uint32_t(p[4]) <<  8) | p[5];
            s.numChunks    = p[6];
        }
        return SUCCESS;

    // ────────────── IMAGE_CHUNK / GIF_FRAME ───
    case MessageType::IMAGE_CHUNK:
    case MessageType::GIF_FRAME:
        {
            const bool gif = (msg.hdr.type == MessageType::GIF_FRAME);
            const uint8_t offBytes = gif ? 4 : 3;
            const size_t  meta     = 2 + offBytes + 2;     // id,id,off,len
            if (len < meta) return INVALID_FORMAT;

            msg.payload.tag = TAG_IMAGE_CHUNK;
            ImageChunk& c   = msg.payload.u.imageChunk;

            c.imgId   = p[0];
            c.chunkId = p[1];
            c.offset  = readBE(p + 2, offBytes);
            c.length  = uint16_t(readBE(p + 2 + offBytes, 2));
            if (meta + c.length != len) return PAYLOAD_LENGTH_MISMATCH;

            c.data = p + meta;            // pointer into original buffer
            return SUCCESS;
        }

    // ────────────── IMAGE_TRANSFER_END / GIF_END ───
    case MessageType::IMAGE_TRANSFER_END:
    case MessageType::GIF_TRANSFER_END:
        if (len != 1) return INVALID_FORMAT;
        msg.payload.tag         = TAG_IMAGE_END;
        msg.payload.u.imageEnd  = ImageEnd{ p[0] };
        return SUCCESS;

    // ─────────────── OPTION_LIST ───────────────
    case MessageType::OPTION_LIST:
        msg.payload.tag = TAG_OPTION_LIST;
        return decodeOptionList(p, len, msg.payload.u.optionList);    // implement like TextBatch

    // ───────────── OPTION_SELECTION_UPDATE ─────
    case MessageType::OPTION_SELECTION_UPDATE:
        if (len != 1) return INVALID_FORMAT;
        msg.payload.tag                 = TAG_OPTION_UPDATE;
        msg.payload.u.optionUpdate.index = p[0];
        return SUCCESS;

    // ─────────────── BACKLIGHT ON / OFF ────────
    case MessageType::BACKLIGHT_ON:
        if (len) return INVALID_FORMAT;
        msg.payload.tag = TAG_BACKLIGHT_ON;
        return SUCCESS;

    case MessageType::BACKLIGHT_OFF:
        if (len) return INVALID_FORMAT;
        msg.payload.tag = TAG_BACKLIGHT_OFF;
        return SUCCESS;

    // ───────────────────── ACK ─────────────────
    case MessageType::ACK:
        if (len != 1) return INVALID_FORMAT;
        msg.payload.tag          = TAG_ACK;
        msg.payload.u.ack.status = static_cast<ErrorCode>(p[0]);
        return SUCCESS;

    // ──────────────────── ERROR ───────────────
    case MessageType::ERROR:
        if (len < 2) return INVALID_FORMAT;
        {
            uint8_t tlen = p[1];
            if (2 + tlen != len || tlen > sizeof(msg.payload.u.error.text))
                return PAYLOAD_LENGTH_MISMATCH;

            msg.payload.tag              = TAG_ERROR;
            msg.payload.u.error.code     = static_cast<ErrorCode>(p[0]);
            msg.payload.u.error.len      = tlen;
            memcpy(msg.payload.u.error.text, p + 2, tlen);
        }
        return SUCCESS;

    // ───────────────── UNKNOWN TYPE ────────────
    default:
        return UNKNOWN_MSG_TYPE;
    }
}


}

#endif



// // SUCCESS!!!
// uint8_t* make_test_text_message() {
//   // SOF, Text_msg, msgid=1, payload_len=18
//   // uint8_t* header = make_msg_header(MessageType::TEXT_BATCH, 0x01, )
//   uint8_t header[5] = {0x7E, 0x01, 0x01, 0x00, 0x12};
//   // bg_color = 0xF79E, txt color = 0x0861
//   uint8_t group_color[4] = {0xF7, 0x9E, 0x08, 0x61};
//   uint8_t num_lines = 1;
//   // x=28, y=28, font=3(Chinese), length=7(bytes)
//   uint8_t text1_header[6] = {0x00, 0x28, 0x00, 0x28, 0x03, 0x07};
//   // text utf-8 bytes
//   uint8_t text_bytes[7] = {0xE4, 0xBD, 0xA0, 0xE5, 0xA5, 0xBD, 0x00};
//   uint8_t* msg_arr = new uint8_t[23];
//   uint8_t* ptr = msg_arr;
//   memcpy(ptr, header, 5);
//   ptr += 5;
//   memcpy(ptr, group_color, 4);
//   ptr += 4;
//   *ptr = num_lines;
//   ptr += 1;
//   memcpy(ptr, text1_header, 6);
//   ptr += 6;
//   memcpy(ptr, text_bytes, 7);
//   return msg_arr;
// }

