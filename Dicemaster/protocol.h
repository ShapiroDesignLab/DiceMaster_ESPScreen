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

// ────────────────────────────────── Ensure SOF_MARKER is available
#ifndef SOF_MARKER
static constexpr uint8_t SOF_MARKER = 0x7E;   // fallback if not defined elsewhere
#endif

namespace Protocol {      // Dice

// ─────────────────────────────────────────────── Low‑level helpers (BigEndian encode)
inline void writeBE(std::vector<uint8_t>& buf, uint32_t value, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        buf.push_back(static_cast<uint8_t>(value >> (8 * (bytes - i - 1))));
}
inline uint32_t readBE(const uint8_t* src, size_t bytes)
{
    uint32_t v = 0;
    for (size_t i = 0; i < bytes; ++i) v = (v << 8) | src[i];
    return v;
}

// ───────────────────────────────────────── Header structure & helpers
struct MessageHeader {
    MessageType type{};      // <- comes from user enum
    uint8_t     id   = 0;
    uint16_t    payloadLen = 0;   // big‑endian length field in raw packet
};

// Encode header inforomation into vector buffer
inline std::vector<uint8_t> encodeHeader(MessageType t, uint8_t id, uint16_t plen)
{
    std::vector<uint8_t> out;
    out.reserve(5);
    out.push_back(SOF_MARKER);
    out.push_back(static_cast<uint8_t>(t));
    out.push_back(id);
    writeBE(out, plen, 2);
    return out;
}


// Decode header from buffer
inline MessageHeader decodeHeader(const uint8_t* buf, size_t len)
{
    if (len < 5) throw std::runtime_error("buffer < header");
    if (buf[0] != SOF_MARKER) throw std::runtime_error("invalid SOF");
    MessageHeader h;
    h.type = static_cast<MessageType>(buf[1]);
    h.id   = buf[2];
    h.payloadLen = static_cast<uint16_t>(buf[3] << 8 | buf[4]);
    return h;
}

// ────────────────────────────────────────────── Payload data structures
struct TextItem { uint16_t x,y; uint8_t fontId; std::vector<uint8_t> text; };
struct TextBatch { uint16_t bgColor, fontColor; std::vector<TextItem> items; };

enum class ImageFormat : uint8_t;        // use enums from user header
enum class ImageResolution : uint8_t;
struct ImageStart  { uint8_t imgId; ImageFormat fmt; ImageResolution res; uint8_t delay; uint32_t total; uint8_t chunks; };
struct ImageChunk  { uint8_t imgId, chunkId; uint32_t offset; std::vector<uint8_t> data; };
struct ImageEnd    { uint8_t imgId; };

struct OptionEntry { bool sel; uint16_t x,y; std::string txt; };
struct OptionList  { std::vector<OptionEntry> entries; };
struct OptionSelectionUpdate { uint8_t index; };

struct BacklightOn  { };   // no payload
struct BacklightOff { };

struct Ack   { ErrorCode status; };
struct Error { ErrorCode code; std::string desc; };

using Payload = std::variant< TextBatch,
                              ImageStart, ImageChunk, ImageEnd,
                              OptionList, OptionSelectionUpdate,
                              ImageStart /*GIF start*/, ImageChunk /*GIF frame*/, ImageEnd /*GIF end*/,
                              BacklightOn, BacklightOff,
                              Ack, Error >;
struct Message { MessageHeader header; Payload payload; };

// ─────────────────────────────────────────── TEXT_BATCH encode/decode
inline std::vector<uint8_t> encode(const TextBatch& tb, uint8_t id)
{
    std::vector<uint8_t> pl;
    writeBE(pl, tb.bgColor, 2);
    writeBE(pl, tb.fontColor, 2);
    if (tb.items.size() > 255) throw std::runtime_error("too many text items");
    pl.push_back(static_cast<uint8_t>(tb.items.size()));
    for (const auto& item : tb.items) {
        writeBE(pl, item.x, 2); writeBE(pl, item.y, 2);
        pl.push_back(item.fontId);
        if (item.text.size() > 255) throw std::runtime_error("text >255");
        pl.push_back(static_cast<uint8_t>(item.text.size()));
        pl.insert(pl.end(), item.text.begin(), item.text.end());
    }
    auto hdr = encodeHeader(MessageType::TEXT_BATCH, id, pl.size());
    hdr.insert(hdr.end(), pl.begin(), pl.end());
    return hdr;
}
inline TextBatch decodeTextBatch(const uint8_t* p, size_t len)
{
    if (len < 5) throw std::runtime_error("TB <5");
    TextBatch tb; tb.bgColor = readBE(p,2); tb.fontColor = readBE(p+2,2);
    uint8_t cnt = p[4]; size_t off = 5;
    while (cnt--) {
        if (off+6 > len) throw std::runtime_error("TB meta truncated");
        TextItem it; it.x = readBE(p+off,2); it.y = readBE(p+off+2,2); it.fontId = p[off+4];
        uint8_t tl = p[off+5]; off += 6;
        if (off+tl > len) throw std::runtime_error("TB text truncated");
        it.text.assign(p+off, p+off+tl); off += tl; tb.items.push_back(std::move(it));
    }
    if (off!=len) throw std::runtime_error("TB leftover");
    return tb;
}

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

// ───────────────────────────────────────── High‑level dispatch
inline Message decodeMessage(const uint8_t* buf,size_t len)
{
    auto hdr=decodeHeader(buf,len); if(len<5+hdr.payloadLen) throw std::runtime_error("buf too short"); const uint8_t* p=buf+5;
    switch(hdr.type){
        case MessageType::TEXT_BATCH:              return {hdr,decodeTextBatch(p,hdr.payloadLen)};
        case MessageType::IMAGE_TRANSFER_START:    return {hdr,decodeImageStart(p,hdr.payloadLen)};
        case MessageType::IMAGE_CHUNK:             return {hdr,decodeImageChunk(p,hdr.payloadLen,false)};
        case MessageType::IMAGE_TRANSFER_END:      return {hdr,decodeImageEnd(p,hdr.payloadLen)};
        case MessageType::GIF_TRANSFER_START:      return {hdr,decodeImageStart(p,hdr.payloadLen)};
        case MessageType::GIF_FRAME:               return {hdr,decodeImageChunk(p,hdr.payloadLen,true)};
        case MessageType::GIF_TRANSFER_END:        return {hdr,decodeImageEnd(p,hdr.payloadLen)};
        case MessageType::OPTION_LIST:             return {hdr,decodeOptionList(p,hdr.payloadLen)};
        case MessageType::OPTION_SELECTION_UPDATE: return {hdr,decodeOptionSelectionUpdate(p,hdr.payloadLen)};
        case MessageType::BACKLIGHT_ON:            return {hdr,BacklightOn{}};
        case MessageType::BACKLIGHT_OFF:           return {hdr,BacklightOff{}};
        case MessageType::ACK:                     return {hdr,decodeAck(p,hdr.payloadLen)};
        case MessageType::ERROR:                   return {hdr,decodeError(p,hdr.payloadLen)};
        default: throw std::runtime_error("unknown MessageType");
    }
}


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

}

#endif
