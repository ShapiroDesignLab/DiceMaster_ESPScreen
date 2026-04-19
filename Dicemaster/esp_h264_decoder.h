#pragma once
#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>
#include "esp_h264/interface/include/esp_h264_dec.h"
#include "esp_h264/sw/include/esp_h264_dec_sw.h"

namespace dice {

// Wraps espressif/esp_h264 software decoder.
// Drop-in replacement for the old H264BsdDecoder — same public API.
class EspH264Decoder {
public:
    EspH264Decoder()  = default;
    ~EspH264Decoder() { destroy(); }

    // Initialise decoder. Optionally supply SPS+PPS Annex B bytes to pre-configure
    // the decoder before the first frame (avoids a one-frame stall on IDR).
    bool init(const uint8_t* sps_pps = nullptr, size_t sps_pps_len = 0);

    // Decode one complete H.264 access unit (Annex B byte-stream).
    // On success writes width*height RGB565 pixels into rgb565_out and returns true.
    // rgb565_out must point to at least width*height*2 bytes of writable PSRAM.
    bool decode_frame(const uint8_t* annex_b, size_t len,
                      uint16_t* rgb565_out, uint16_t width, uint16_t height);

    void reset();
    void destroy();
    bool is_initialized() const { return _handle != nullptr; }

private:
    esp_h264_dec_handle_t _handle = nullptr;

    // Feed a byte stream to the decoder, advancing by in_frame.consume each call.
    // Sets got_frame=true and converts I420→RGB565 when a picture is ready.
    bool process_stream(const uint8_t* data, size_t len,
                        uint16_t* rgb565_out, uint16_t width, uint16_t height,
                        bool& got_frame);
};

} // namespace dice
