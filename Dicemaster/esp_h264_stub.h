#pragma once
// Stub for esp_h264 — replace with real component when building with IDF.
// Provides minimum type definitions for structural compilation.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef esp_h264_dec_handle_t
typedef void* esp_h264_dec_handle_t;
#endif

#ifndef esp_err_t
typedef int   esp_err_t;
#endif

#ifndef ESP_OK
#define ESP_OK   0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL (-1)
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
} esp_h264_dec_out_frame_t;

// Stub functions — do nothing, return ESP_FAIL to signal not implemented
static inline esp_err_t esp_h264_dec_sw_new(void* cfg, esp_h264_dec_handle_t* out) {
    (void)cfg; (void)out; return ESP_FAIL;
}
static inline esp_err_t esp_h264_dec_open(esp_h264_dec_handle_t h) {
    (void)h; return ESP_FAIL;
}
static inline esp_err_t esp_h264_dec_process(esp_h264_dec_handle_t h, const uint8_t* in, size_t in_len, esp_h264_dec_out_frame_t* out) {
    (void)h; (void)in; (void)in_len; (void)out; return ESP_FAIL;
}
static inline esp_err_t esp_h264_dec_close(esp_h264_dec_handle_t h) {
    (void)h; return ESP_FAIL;
}

#ifdef __cplusplus
}
#endif
