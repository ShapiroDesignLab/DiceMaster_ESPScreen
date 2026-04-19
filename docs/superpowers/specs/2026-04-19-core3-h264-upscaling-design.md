# DiceMaster ESP32: Core 3.x Upgrade + H.264 + 2× Upscaling

**Date:** 2026-04-19  
**Status:** Approved  
**Branch target:** `feature-h264` → `main`

---

## Goal

Upgrade the firmware to Arduino ESP32 Core 3.x (IDF 5.x) to unlock the official `espressif/esp_h264` software decoder, migrate the display driver to the IDF-native `esp_lcd` API, and add a clean 2× upscaling path so callers can push 240×240 content that renders at 480×480.

---

## Section 1: Dependencies & Core Upgrade

| Library | Before | After | Notes |
|---|---|---|---|
| Arduino ESP32 core | 2.0.17 (IDF 4.4) | 3.2.0 (IDF 5.x) | Required for `esp_h264` |
| ESP32DMASPI | library manager | removed | Already ported into `ESP32DMASPIStream.h` |
| Arduino_ESP32RGBPanel | library manager | removed | Replaced by direct `esp_lcd` IDF calls |
| GFX Library for Arduino | 1.4.9 | 1.4.9 (keep) | Used only for `Arduino_Canvas` framebuffer |
| JPEGDEC | 1.8.2 | 1.8.2 (keep) | No IDF dependency |
| U8g2 | 2.35.30 | 2.35.30 (keep) | No IDF dependency |
| espressif/esp_h264 | — | source copy in `Dicemaster/esp_h264/` | Not in Library Manager; copy from `~/.arduino15/packages/esp32/hardware/esp32/<ver>/libraries/esp_h264/` or clone from `https://github.com/espressif/idf-extra-components` |

**LVGL:** Deferred. LVGL cannot support all Unicode/niche-language scripts without impractically large pre-compiled font files, and has no text-shaping engine for Devanagari/Arabic. U8g2 unifont (55,000+ glyphs on-demand from flash) remains the text renderer. LVGL may be layered in later as a pure animation/UI skin.

---

## Section 2: Display Layer Migration

### Current state
`screen.h` holds `Arduino_RGB_Display* _display` (wraps `Arduino_ESP32RGBPanel`). Drawing goes through the GFX API; flush is implicit.

### New state

**`screen.h` changes:**
- Remove `Arduino_RGB_Display*`, `Arduino_GFX_Library.h` panel headers
- Add `esp_lcd_panel_handle_t _panel`
- Add `Arduino_Canvas* _canvas` — 480×480×2 byte off-screen framebuffer, allocated in PSRAM

**Initialization (`screen.cpp`):**
```
esp_lcd_rgb_panel_config_t cfg = { /* ST7701S timings + GPIO from existing constants */ };
esp_lcd_new_rgb_panel(&cfg, &_panel);
esp_lcd_panel_reset(_panel);
esp_lcd_panel_init(_panel);
_canvas = new Arduino_Canvas(480, 480, nullptr);  // nullptr = software-only, no hardware GFX
```

**Draw path (unchanged at call sites):**  
All existing callers (`draw_bmp565_rotated`, U8g2 text, video frame blits) already write pixel data to a buffer; they will be redirected to target `_canvas->getBuffer()`.

**Flush (`screen->update()`):**
```cpp
esp_lcd_panel_draw_bitmap(_panel, 0, 0, 480, 480, _canvas->getBuffer());
```

`DecodingHandler`, `media.h`, and the protocol layer are untouched by this change.

---

## Section 3: H.264 Decoder

### Files

| File | Action |
|---|---|
| `Dicemaster/esp_h264_decoder.h` | New — `EspH264Decoder` class |
| `Dicemaster/esp_h264_decoder.cpp` | New — implementation |
| `Dicemaster/esp_h264/` | New directory — copy of `espressif/esp_h264` component source |
| `Dicemaster/h264bsd_decoder.h` | Delete |
| `Dicemaster/h264bsd_decoder.cpp` | Delete |
| `Dicemaster/esp_h264_stub.h` | Delete |
| `Dicemaster/video_stream.h` | Update: `H264BsdDecoder*` → `EspH264Decoder*`, swap include |
| `Dicemaster/video_stream.cpp` | No logic changes needed |

### Class interface (`EspH264Decoder`)

```cpp
namespace dice {
class EspH264Decoder {
public:
    // Optionally supply SPS+PPS Annex B bytes to pre-configure the decoder.
    bool init(const uint8_t* sps_pps = nullptr, size_t sps_pps_len = 0);

    // Decode one complete H.264 access unit (one or more NALUs, Annex B byte-stream).
    // On success writes width*height RGB565 pixels into rgb565_out and returns true.
    bool decode_frame(const uint8_t* annex_b, size_t len,
                      uint16_t* rgb565_out, uint16_t width, uint16_t height);

    void reset();    // destroy + re-init
    void destroy();
    bool is_initialized() const { return _handle != nullptr; }

private:
    esp_h264_dec_handle_t _handle = nullptr;

    // Split Annex B stream into NALUs and feed each to esp_h264_dec_process().
    // Returns true and sets got_frame when a picture is available.
    bool feed_nalus(const uint8_t* data, size_t len,
                    uint16_t* rgb565_out, uint16_t width, uint16_t height,
                    bool& got_frame);
};
}
```

### Implementation details

`esp_h264_dec_process()` consumes one NALU per call. `feed_nalus()` scans for `0x000001` / `0x00000001` start codes and submits each NALU in a loop. When `out_frame.outbuf` is non-null after a slice NALU, call `VideoFrame::yuv420_to_rgb565()` (already implemented) to produce RGB565.

```
esp_h264_dec_cfg_sw_t cfg = { .pic_type = ESP_H264_RAW_FMT_I420 };
esp_h264_dec_sw_new(&cfg, &_handle);
esp_h264_dec_open(_handle);
// per NALU:
esp_h264_dec_in_frame_t  in  = { .raw_data = {.buffer = nalu, .len = nalu_len} };
esp_h264_dec_out_frame_t out = {0};
esp_h264_dec_process(_handle, &in, &out);
// out.outbuf != nullptr → picture ready (I420)
```

**Encoding requirement:** H.264 Baseline Profile, level 3.0 or lower. No B-frames. Recommended: 480×480 or 240×240 at ≤10 fps.

**Expected performance (ESP32-S3 @ 240 MHz):**
- 480×480: ~9–11 fps (software decoder)
- 240×240 → upscaled: ~20–30 fps

---

## Section 4: 2× Upscaling API

### New helper (`media.h` / `media.cpp`)

```cpp
// Expands a w×h RGB565 image to 2w×2h in-place into dst (must be ≥ 4*w*h bytes).
// Uses 32-bit horizontal writes + row memcpy for cache efficiency.
static void upscale_bmp565_2x(const uint16_t* src, uint16_t* dst, int w, int h);
```

Algorithm per source row:
1. Walk source row left→right: write each pixel as a 32-bit pair (`(p << 16) | p`) into the dst row
2. `memcpy` the expanded dst row into the row immediately below it

### New video path (`media.h` / `media.cpp`)

```cpp
// Decode 240×240 I420 and upscale to 480×480 RGB565 in one pass.
// Uses a ~115KB PSRAM temp buffer (240×240×2).
static void yuv420_to_rgb565_2x(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                  uint16_t* dst_480, int src_w, int src_h);
```

### Updated JPEG path (`media.cpp`)

`JPEGDraw240` callback: replace the existing nested per-pixel `for` loop with a call to `upscale_bmp565_2x()` per MCU row block.

### New `Screen` API (`screen.h` / `screen.cpp`)

```cpp
// Draw a sub-480 RGB565 image upscaled 2× at the center/origin with rotation.
void draw_bmp565_2x(const uint16_t* src, int src_w, int src_h, Rotation rot);
```

Internally: upscale into a PSRAM temp buffer, then pass to existing `draw_bmp565_rotated()`.

---

## Data Flow (post-migration)

```
SPI Slave HW
  → decode_task (FreeRTOS)
  → DecodingHandler
      ├─ VideoStream → EspH264Decoder → VideoFrame (RGB565 in PSRAM pool) → Screen queue
      ├─ Image       → JPEGDEC (via JPEGDraw240 + upscale_bmp565_2x)      → Screen queue
      └─ TextGroup   → U8g2 → Arduino_Canvas buffer
  → screen->update()
      → esp_lcd_panel_draw_bitmap(_panel, canvas->getBuffer())
```

---

## Section 5: Embedded Test Video

A self-contained on-device test that decodes and displays a short H.264 clip without any SPI master, exercising the full decode → RGB565 → screen pipeline in `TESTING` mode.

### Video content

A synthetic rotating U-M block "M" (maize `#FFCB05` on blue `#00274C` background), generated entirely by the build script — no external file download needed.

- **Resolution:** 480×480
- **Duration:** ~3 seconds, 5 fps = 15 frames
- **Profile:** H.264 Baseline, level 3.0, CRF 28, yuv420p
- **Expected compressed size:** ~80–200 KB (fits easily in 16MB flash)

### Script: `scripts/gen_test_video.py`

Generates the video and C header in one command (`python3 scripts/gen_test_video.py`):

1. Renders 15 PNG frames using Pillow: 480×480 blue background, large block "M" in maize, rotated 0°–360° evenly across frames
2. Runs `ffmpeg` to encode all frames to raw Annex B H.264 (`test_video.h264`): `-c:v libx264 -profile:v baseline -level 3.0 -crf 28 -pix_fmt yuv420p -f h264`
3. Parses the Annex B byte stream to find access unit (AU) boundaries by scanning for start codes (`0x000001` / `0x00000001`) followed by NAL unit type 1 (non-IDR slice) or 5 (IDR slice) — each such NAL starts a new AU
4. Outputs `Dicemaster/test_video.h`:

```cpp
#pragma once
#include <stdint.h>
static constexpr int TEST_VIDEO_FRAMES = 15;
static constexpr int TEST_VIDEO_WIDTH  = 480;
static constexpr int TEST_VIDEO_HEIGHT = 480;
static constexpr int TEST_VIDEO_FPS    = 5;
// Byte length of each access unit in TEST_VIDEO_DATA
static const uint32_t TEST_VIDEO_FRAME_SIZES[] PROGMEM = { ... };
// Concatenated Annex B access units
static const uint8_t  TEST_VIDEO_DATA[]         PROGMEM = { ... };
```

### On-device test

New test function in `TestSuite` activated by `SystemMode::TESTING`:

```
For each frame i in [0, TEST_VIDEO_FRAMES):
  1. Compute data pointer = TEST_VIDEO_DATA + sum(FRAME_SIZES[0..i-1])
  2. EspH264Decoder::decode_frame(ptr, FRAME_SIZES[i], slot, 480, 480)
  3. screen->draw_bmp565_rotated(slot, ROT_0)
  4. delay(1000 / TEST_VIDEO_FPS)
Loop indefinitely
```

The test bypasses `VideoStream` and `DecodingHandler` entirely to isolate just the decoder + display path.

---

## Files to Create

- `Dicemaster/esp_h264_decoder.h`
- `Dicemaster/esp_h264_decoder.cpp`
- `Dicemaster/esp_h264/` (component source copy)
- `scripts/gen_test_video.py`
- `Dicemaster/test_video.h` (generated by script, committed to repo)

## Files to Modify

- `Dicemaster/screen.h` — swap display type, add canvas
- `Dicemaster/screen.cpp` — init + flush rewrite
- `Dicemaster/video_stream.h` — swap decoder type
- `Dicemaster/media.h` — add `upscale_bmp565_2x`, `yuv420_to_rgb565_2x`, `draw_bmp565_2x` declarations
- `Dicemaster/media.cpp` — implement helpers, optimize `JPEGDraw240`
- `Dicemaster/Dicemaster.ino` — add video test case to `TESTING` mode

## Files to Delete

- `Dicemaster/h264bsd_decoder.h`
- `Dicemaster/h264bsd_decoder.cpp`
- `Dicemaster/esp_h264_stub.h`

---

## Out of Scope

- LVGL integration (deferred)
- Protocol changes
- Master-side streaming changes
- B-frame or CABAC H.264 support
