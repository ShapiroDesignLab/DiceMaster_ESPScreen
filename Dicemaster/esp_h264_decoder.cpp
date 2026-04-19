#include "esp_h264_decoder.h"
#include "media.h"

namespace dice {

bool EspH264Decoder::init(const uint8_t* sps_pps, size_t sps_pps_len) {
    destroy();

    esp_h264_dec_cfg_sw_t cfg = { .pic_type = ESP_H264_RAW_FMT_I420 };
    if (esp_h264_dec_sw_new(&cfg, &_handle) != ESP_H264_ERR_OK) {
        Serial.println("[H264] esp_h264_dec_sw_new failed");
        _handle = nullptr;
        return false;
    }
    if (esp_h264_dec_open(_handle) != ESP_H264_ERR_OK) {
        Serial.println("[H264] esp_h264_dec_open failed");
        esp_h264_dec_del(_handle);
        _handle = nullptr;
        return false;
    }

    // Feed optional SPS+PPS so the decoder pre-allocates reference buffers.
    if (sps_pps && sps_pps_len > 0) {
        bool dummy = false;
        process_stream(sps_pps, sps_pps_len, nullptr, 0, 0, dummy);
    }

    Serial.println("[H264] Decoder initialized");
    return true;
}

void EspH264Decoder::destroy() {
    if (_handle) {
        esp_h264_dec_close(_handle);
        esp_h264_dec_del(_handle);
        _handle = nullptr;
    }
}

void EspH264Decoder::reset() {
    init();
}

bool EspH264Decoder::decode_frame(const uint8_t* annex_b, size_t len,
                                   uint16_t* rgb565_out, uint16_t width, uint16_t height) {
    if (!_handle || !annex_b || len == 0 || !rgb565_out) return false;
    bool got_frame = false;
    process_stream(annex_b, len, rgb565_out, width, height, got_frame);
    return got_frame;
}

// Official usage pattern from esp_h264_types.h @code example:
// feed the buffer in a loop, advancing by in_frame.consume each iteration.
bool EspH264Decoder::process_stream(const uint8_t* data, size_t len,
                                     uint16_t* rgb565_out, uint16_t width, uint16_t height,
                                     bool& got_frame) {
    got_frame = false;

    esp_h264_dec_in_frame_t in_frame = {};
    // The API takes a non-const buffer pointer; we only read from it.
    in_frame.raw_data.buffer = const_cast<uint8_t*>(data);
    in_frame.raw_data.len    = static_cast<uint32_t>(len);

    while (in_frame.raw_data.len > 0) {
        esp_h264_dec_out_frame_t out_frame = {};

        esp_h264_err_t ret = esp_h264_dec_process(_handle, &in_frame, &out_frame);

        // Check error before advancing — matches the official usage pattern in esp_h264_types.h.
        if (ret != ESP_H264_ERR_OK) {
            Serial.printf("[H264] esp_h264_dec_process error: %d\n", (int)ret);
            return false;
        }

        if (in_frame.consume == 0) break;  // no progress — guard against infinite loop
        if (in_frame.consume > in_frame.raw_data.len) {
            Serial.printf("[H264] consume (%lu) > remaining (%lu)\n",
                          (unsigned long)in_frame.consume,
                          (unsigned long)in_frame.raw_data.len);
            return false;
        }
        in_frame.raw_data.buffer += in_frame.consume;
        in_frame.raw_data.len    -= in_frame.consume;

        // outbuf is reused on the next call to esp_h264_dec_process — consume it immediately.
        if (out_frame.outbuf != nullptr && out_frame.out_size > 0 && rgb565_out) {
            // out_frame.outbuf is I420 planar: Y(w*h), U(w/2*h/2), V(w/2*h/2)
            const uint8_t* y = out_frame.outbuf;
            const uint8_t* u = y + (uint32_t)width * height;
            const uint8_t* v = u + ((uint32_t)(width / 2)) * (height / 2);
            VideoFrame::yuv420_to_rgb565(y, u, v, rgb565_out, width, height);
            got_frame = true;
            return true;
        }
    }

    return true;
}

} // namespace dice
