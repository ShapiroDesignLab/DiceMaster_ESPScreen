# DiceMaster Core 3.x + H.264 + 2× Upscaling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the firmware to Arduino ESP32 Core 3.x, migrate the display driver to IDF-native `esp_lcd`, add `espressif/esp_h264` software decoding, and add a 2× pixel-doubling path with an on-device test video.

**Architecture:** `esp_lcd_new_rgb_panel()` drives the ST7701S RGB panel directly; `Arduino_Canvas` serves as the software framebuffer for all drawing; after each frame is drawn, `esp_lcd_panel_draw_bitmap()` DMA-pushes the canvas to the display. `EspH264Decoder` wraps `esp_h264_dec_process()` in a NALU-splitting loop and converts I420 → RGB565 via the existing `VideoFrame::yuv420_to_rgb565()`.

**Tech Stack:** Arduino ESP32 Core 3.2.0 (IDF 5.x), `espressif/esp_h264` (idf-extra-components), `Arduino_GFX_Library` 1.4.9 (`Arduino_Canvas` only), `JPEGDEC` 1.8.2, `U8g2` 2.35.30, Python 3 + Pillow + ffmpeg (for test video generation).

**Working directory:** `.worktrees/feature-h264/` — all file paths below are relative to it.

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `Dicemaster/screen.h` | Modify | Swap `Arduino_RGB_Display*` → `Arduino_Canvas*`, add `esp_lcd_panel_handle_t` |
| `Dicemaster/screen.cpp` | Modify | Rewrite constructor (esp_lcd + expander init), add flush after render |
| `Dicemaster/esp_h264_decoder.h` | Create | `EspH264Decoder` interface |
| `Dicemaster/esp_h264_decoder.cpp` | Create | NALU-loop decode + I420→RGB565 |
| `Dicemaster/esp_h264/` | Create | Copy of `espressif/esp_h264` component source |
| `Dicemaster/video_stream.h` | Modify | Swap `H264BsdDecoder*` → `EspH264Decoder*` |
| `Dicemaster/media.h` | Modify | Declare `upscale_bmp565_2x`, `yuv420_to_rgb565_2x` |
| `Dicemaster/media.cpp` | Modify | Implement both helpers, optimize `JPEGDraw240` |
| `Dicemaster/screen.h` | Modify | Declare `draw_bmp565_2x` |
| `Dicemaster/screen.cpp` | Modify | Implement `draw_bmp565_2x` |
| `Dicemaster/tests.h` | Modify | Add `test_video_playback()` |
| `scripts/gen_test_video.py` | Create | Generates rotating U-M logo H.264 + C header |
| `Dicemaster/test_video.h` | Create (generated) | Embedded Annex B byte array with frame offsets |
| `Dicemaster/h264bsd_decoder.h` | Delete | Replaced by EspH264Decoder |
| `Dicemaster/h264bsd_decoder.cpp` | Delete | — |
| `Dicemaster/esp_h264_stub.h` | Delete | — |

---

## Task 1: Upgrade Arduino Core to 3.2.0

**Files:** Arduino IDE Boards Manager only

- [ ] **Step 1: Open Arduino IDE → Tools → Board → Boards Manager**

  Search "esp32 by Espressif Systems". Install version **3.2.0** (or the latest 3.x stable). Keep 2.0.17 listed; only the selected board version matters.

- [ ] **Step 2: Select the correct board**

  Tools → Board → "Adafruit Qualia ESP32-S3 RGB666" (should appear under the Core 3.x install).

- [ ] **Step 3: Attempt to compile `Dicemaster.ino`**

  Sketch → Verify/Compile (Ctrl+R / Cmd+R).

  **Expected:** errors about `Arduino_ESP32RGBPanel` not declared, `rgbpanel` not found. These are expected because `Arduino_ESP32RGBPanel` is conditionally excluded in GFX 1.4.9 on Core 3.x. Everything else in the library compiles. **Do not fix yet.**

- [ ] **Step 4: Commit the board selection note**

  ```bash
  git add -A
  git commit -m "chore: document Core 3.x upgrade in progress"
  ```

---

## Task 2: Fetch espressif/esp_h264 Component

**Files:**
- Create: `Dicemaster/esp_h264/` (directory)

- [ ] **Step 1: Clone only the esp_h264 subtree from idf-extra-components**

  ```bash
  cd /tmp
  git clone --depth=1 --filter=blob:none --sparse \
    https://github.com/espressif/idf-extra-components.git idf-extra-components
  cd idf-extra-components
  git sparse-checkout set esp_h264
  ```

- [ ] **Step 2: Copy the component source into the sketch directory**

  ```bash
  cp -r /tmp/idf-extra-components/esp_h264 \
    .worktrees/feature-h264/Dicemaster/esp_h264/
  ```

  Verify the resulting structure:
  ```
  Dicemaster/esp_h264/
  ├── include/
  │   ├── esp_h264_dec.h
  │   └── esp_h264_types.h
  └── src/
      └── sw_dec/
          ├── h264_dec.c
          └── ...
  ```

  If the structure differs, find `esp_h264_dec.h` and note its actual location for Task 4.

- [ ] **Step 3: Verify the key API symbols are present**

  ```bash
  grep -r "esp_h264_dec_sw_new\|esp_h264_dec_process\|esp_h264_dec_open" \
    .worktrees/feature-h264/Dicemaster/esp_h264/
  ```

  Expected: matches in at least one `.h` file.

- [ ] **Step 4: Commit**

  ```bash
  git -C .worktrees/feature-h264 add Dicemaster/esp_h264/
  git -C .worktrees/feature-h264 commit -m "feat: add espressif/esp_h264 component source"
  ```

---

## Task 3: Migrate Screen to esp_lcd + Arduino_Canvas

**Files:**
- Modify: `Dicemaster/screen.h`
- Modify: `Dicemaster/screen.cpp`

The GFX library already conditionally excludes `Arduino_ESP32RGBPanel` on Core 3.x. We keep `Arduino_XCA9554SWSPI` (for ST7701S SPI init and backlight/buttons) and add `esp_lcd_new_rgb_panel()` for RGB driving. `Arduino_Canvas` replaces `Arduino_RGB_Display` as the GFX drawing target; after each frame, `esp_lcd_panel_draw_bitmap()` pushes the canvas framebuffer to the hardware.

- [ ] **Step 1: Update `Dicemaster/screen.h`**

  Replace the three private hardware member declarations and add `esp_lcd`:

  ```cpp
  // At top of file, add after existing includes:
  #include <esp_lcd_panel_rgb.h>
  #include <esp_lcd_panel_ops.h>
  ```

  Replace the three private members:
  ```cpp
  // REMOVE these three lines:
  Arduino_XCA9554SWSPI* expander;
  Arduino_ESP32RGBPanel* rgbpanel;
  Arduino_RGB_Display* gfx;

  // ADD these three lines:
  Arduino_XCA9554SWSPI* expander;
  esp_lcd_panel_handle_t _panel = nullptr;
  Arduino_Canvas* gfx;
  ```

  Full updated private section (replace lines 20–23 in screen.h):
  ```cpp
  private:
      Arduino_XCA9554SWSPI* expander;
      esp_lcd_panel_handle_t _panel = nullptr;
      Arduino_Canvas* gfx;
  ```

- [ ] **Step 2: Rewrite `Screen::Screen()` constructor in `screen.cpp`**

  Replace the entire constructor body (lines 414–468 in the original). The new constructor:

  ```cpp
  Screen::Screen()
      : expander(new Arduino_XCA9554SWSPI(
            PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI, &Wire, 0x3F))
      , gfx(new Arduino_Canvas(480, 480, nullptr))
      , media_queue(nullptr)
      , screen_buffer((uint16_t*)ps_malloc(480 * 480 * sizeof(uint16_t)))
      , current_disp(nullptr)
      , queue_mutex(nullptr)
      , current_gfx_rotation(Rotation::ROT_0)
  {
      // I2C for XCA9554 expander (backlight, buttons, ST7701S SPI)
      Wire.begin();
      Wire.setClock(1000000);

      // Send ST7701S initialization sequence through the XCA9554 SPI expander.
      // batchOperation() interprets the same op-code bytecode that Arduino_RGB_Display used.
      expander->begin();
      expander->batchOperation(tl040wvs03_init_operations, sizeof(tl040wvs03_init_operations));
      Serial.println("ST7701S init sequence sent");

      // Configure esp_lcd RGB panel (IDF 5.x API).
      // data_gpio_nums ordering: B[0:4], G[0:5], R[0:4] — matches Arduino_ESP32RGBPanel default.
      const esp_lcd_rgb_panel_config_t panel_cfg = {
          .clk_src = LCD_CLK_SRC_DEFAULT,
          .timings = {
              .pclk_hz         = 16 * 1000 * 1000,
              .h_res           = 480,
              .v_res           = 480,
              .hsync_pulse_width = 2,
              .hsync_back_porch  = 44,
              .hsync_front_porch = 50,
              .vsync_pulse_width = 2,
              .vsync_back_porch  = 18,
              .vsync_front_porch = 16,
              .flags = {
                  .hsync_idle_low = 1,
                  .vsync_idle_low = 1,
              },
          },
          .data_width       = 16,
          .psram_trans_align = 64,
          .hsync_gpio_num   = TFT_HSYNC,
          .vsync_gpio_num   = TFT_VSYNC,
          .de_gpio_num      = TFT_DE,
          .pclk_gpio_num    = TFT_PCLK,
          .disp_gpio_num    = GPIO_NUM_NC,
          .data_gpio_nums   = {
              TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,   // data[0:4]  = B[0:4]
              TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,  // data[5:10] = G[0:5]
              TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,   // data[11:15] = R[0:4]
          },
          .flags = { .fb_in_psram = 1 },
      };
      ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &_panel));
      ESP_ERROR_CHECK(esp_lcd_panel_reset(_panel));
      ESP_ERROR_CHECK(esp_lcd_panel_init(_panel));
      Serial.println("esp_lcd RGB panel initialized");

      // Initialize the software canvas (replaces Arduino_RGB_Display as the GFX drawing target).
      gfx->begin();
      gfx->fillScreen(DICE_BLACK);
      gfx->setUTF8Print(true);

      // Backlight via XCA9554 expander
      expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
      expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

      // Push initial black frame to hardware
      esp_lcd_panel_draw_bitmap(_panel, 0, 0, 480, 480, gfx->getFramebuffer());

      // Initialize thread-safe media queue and mutex
      media_queue = xQueueCreate(SCREEN_MEDIA_QUEUE_SIZE, sizeof(MediaContainer*));
      if (!media_queue) {
          Serial.println("[SCREEN] FATAL: Failed to create media queue");
          while (1) delay(1000);
      }
      queue_mutex = xSemaphoreCreateMutex();
      if (!queue_mutex) {
          Serial.println("[SCREEN] FATAL: Failed to create queue mutex");
          vQueueDelete(media_queue);
          media_queue = nullptr;
          while (1) delay(1000);
      }
      Serial.println("[SCREEN] Queue initialized");
      Serial.println("Screen Initialized!");
  }
  ```

- [ ] **Step 3: Add flush call to `display_next()` in `screen.cpp`**

  `Arduino_Canvas` is not auto-flushing. After `current_disp->trigger_display()` (the last line of the rendering switch block, around line 410), add:

  ```cpp
  current_disp->trigger_display();
  // Push canvas framebuffer to the hardware via DMA
  esp_lcd_panel_draw_bitmap(_panel, 0, 0, 480, 480, gfx->getFramebuffer());
  ```

- [ ] **Step 4: Update `draw_bmp565()` — `getBuffer()` → `getFramebuffer()`**

  `Arduino_Canvas` uses `getFramebuffer()`, not `getBuffer()`. Find any call to `gfx->getBuffer()` (there are none in the current code, but verify) and also update:

  In `draw_bmp565_rotated()`, the existing code already calls `gfx->draw16bitRGBBitmap()` which targets the canvas — no change needed there.

- [ ] **Step 5: Compile — expect success**

  Sketch → Verify/Compile. Expected: **no errors**. If `esp_lcd_rgb_panel_config_t` field names fail, check the IDF 5.x header at `~/.arduino15/packages/esp32/hardware/esp32/3.x.x/tools/sdk/esp32s3/include/esp_lcd/include/esp_lcd_panel_rgb.h` and adjust field names to match.

  Common IDF 5.x compatibility fixes if needed:
  - If `LCD_CLK_SRC_DEFAULT` is missing, use `LCD_CLK_SRC_PLL160M`
  - If `flags.hsync_idle_low` is missing, remove the inner `.flags` nesting and use `timings.flags.hsync_idle_low`

- [ ] **Step 6: Flash and verify display works**

  Flash to board. Expected on Serial: "ST7701S init sequence sent", "esp_lcd RGB panel initialized", "Screen Initialized!", startup logo visible.

  If display shows color-shifted or inverted image, swap B and R groups in `data_gpio_nums`:
  ```cpp
  // Try swapped order:
  .data_gpio_nums = {
      TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,   // data[0:4]  = R
      TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
      TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,   // data[11:15] = B
  },
  ```

- [ ] **Step 7: Commit**

  ```bash
  git -C .worktrees/feature-h264 add Dicemaster/screen.h Dicemaster/screen.cpp
  git -C .worktrees/feature-h264 commit -m "feat: migrate display to esp_lcd + Arduino_Canvas"
  ```

---

## Task 4: Create EspH264Decoder

**Files:**
- Create: `Dicemaster/esp_h264_decoder.h`
- Create: `Dicemaster/esp_h264_decoder.cpp`

- [ ] **Step 1: Write `Dicemaster/esp_h264_decoder.h`**

  ```cpp
  #pragma once
  #include <stdint.h>
  #include <stddef.h>
  #include <Arduino.h>
  #include "esp_h264/include/esp_h264_dec.h"

  namespace dice {

  // Wraps espressif/esp_h264 software decoder.
  // API is identical to the old H264BsdDecoder so video_stream.cpp needs no logic changes.
  class EspH264Decoder {
  public:
      EspH264Decoder()  = default;
      ~EspH264Decoder() { destroy(); }

      // Initialise decoder. Optionally supply SPS+PPS Annex B bytes to pre-configure
      // the decoder before the first frame (avoids a one-frame stall).
      bool init(const uint8_t* sps_pps = nullptr, size_t sps_pps_len = 0);

      // Decode one complete H.264 access unit in Annex B byte-stream format.
      // On success writes width*height RGB565 pixels into rgb565_out and returns true.
      // rgb565_out must point to at least width*height*2 bytes of writable memory.
      bool decode_frame(const uint8_t* annex_b, size_t len,
                        uint16_t* rgb565_out, uint16_t width, uint16_t height);

      // Destroy + re-init (e.g. at stream boundaries or after a decode error).
      void reset();
      void destroy();

      bool is_initialized() const { return _handle != nullptr; }

  private:
      esp_h264_dec_handle_t _handle = nullptr;

      // Feed an Annex B byte stream as individual NALUs to esp_h264_dec_process().
      // Returns true and sets got_frame=true when a picture is written to rgb565_out.
      bool feed_nalus(const uint8_t* data, size_t len,
                      uint16_t* rgb565_out, uint16_t width, uint16_t height,
                      bool& got_frame);
  };

  } // namespace dice
  ```

- [ ] **Step 2: Write `Dicemaster/esp_h264_decoder.cpp`**

  ```cpp
  #include "esp_h264_decoder.h"
  #include "media.h"   // VideoFrame::yuv420_to_rgb565
  #include <esp_log.h>

  namespace dice {

  // ---------------------------------------------------------------------------
  // Public API
  // ---------------------------------------------------------------------------

  bool EspH264Decoder::init(const uint8_t* sps_pps, size_t sps_pps_len) {
      destroy();

      esp_h264_dec_cfg_sw_t cfg = { .pic_type = ESP_H264_RAW_FMT_I420 };
      if (esp_h264_dec_sw_new(&cfg, &_handle) != ESP_OK) {
          Serial.println("[H264] esp_h264_dec_sw_new failed");
          _handle = nullptr;
          return false;
      }
      if (esp_h264_dec_open(_handle) != ESP_OK) {
          Serial.println("[H264] esp_h264_dec_open failed");
          esp_h264_dec_del(_handle);
          _handle = nullptr;
          return false;
      }

      // Feed optional SPS+PPS so the decoder pre-allocates reference buffers.
      if (sps_pps && sps_pps_len > 0) {
          bool dummy = false;
          feed_nalus(sps_pps, sps_pps_len, nullptr, 0, 0, dummy);
      }

      Serial.println("[H264] Decoder initialised");
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
      // esp_h264 has no reset; cheapest path is destroy + re-init.
      init();
  }

  bool EspH264Decoder::decode_frame(const uint8_t* annex_b, size_t len,
                                     uint16_t* rgb565_out, uint16_t width, uint16_t height) {
      if (!_handle || !annex_b || len == 0 || !rgb565_out) return false;
      bool got_frame = false;
      feed_nalus(annex_b, len, rgb565_out, width, height, got_frame);
      return got_frame;
  }

  // ---------------------------------------------------------------------------
  // Private helper
  // ---------------------------------------------------------------------------

  bool EspH264Decoder::feed_nalus(const uint8_t* data, size_t len,
                                   uint16_t* rgb565_out, uint16_t width, uint16_t height,
                                   bool& got_frame) {
      got_frame = false;
      size_t offset = 0;

      while (offset < len) {
          esp_h264_dec_in_frame_t  in_frame  = {};
          esp_h264_dec_out_frame_t out_frame = {};
          in_frame.raw_data.buffer = data + offset;
          in_frame.raw_data.len    = static_cast<uint32_t>(len - offset);

          esp_err_t ret = esp_h264_dec_process(_handle, &in_frame, &out_frame);

          uint32_t consumed = in_frame.consume;
          if (consumed == 0) break;  // no progress — avoid infinite loop
          offset += consumed;

          if (ret == ESP_OK && out_frame.outbuf != nullptr && rgb565_out) {
              uint16_t dw = out_frame.width  ? out_frame.width  : width;
              uint16_t dh = out_frame.height ? out_frame.height : height;
              if (dw > width)  dw = width;
              if (dh > height) dh = height;

              // out_frame.outbuf is I420 planar: Y(dw*dh), U(dw/2*dh/2), V(dw/2*dh/2)
              const uint8_t* y = out_frame.outbuf;
              const uint8_t* u = y + dw * dh;
              const uint8_t* v = u + (dw / 2) * (dh / 2);
              VideoFrame::yuv420_to_rgb565(y, u, v, rgb565_out, dw, dh);
              got_frame = true;
              return true;
          }

          if (ret != ESP_OK) {
              Serial.printf("[H264] esp_h264_dec_process error: %d\n", (int)ret);
              return false;
          }
      }

      return true;
  }

  } // namespace dice
  ```

- [ ] **Step 3: Compile**

  Sketch → Verify/Compile. Expected: no errors. If `esp_h264_dec_cfg_sw_t` or `ESP_H264_RAW_FMT_I420` are not found, adjust the include path to wherever `esp_h264_dec.h` landed in step 2 of Task 2.

  If `in_frame.consume` doesn't exist in the actual header, look at the actual `esp_h264_dec_in_frame_t` struct definition and use the correct field name for "bytes consumed".

- [ ] **Step 4: Commit**

  ```bash
  git -C .worktrees/feature-h264 add \
    Dicemaster/esp_h264_decoder.h Dicemaster/esp_h264_decoder.cpp
  git -C .worktrees/feature-h264 commit -m "feat: add EspH264Decoder wrapping espressif/esp_h264"
  ```

---

## Task 5: Wire EspH264Decoder into VideoStream + Delete Old Files

**Files:**
- Modify: `Dicemaster/video_stream.h`
- Delete: `Dicemaster/h264bsd_decoder.h`, `Dicemaster/h264bsd_decoder.cpp`, `Dicemaster/esp_h264_stub.h`

- [ ] **Step 1: Update `video_stream.h`**

  Change line 7:
  ```cpp
  // BEFORE:
  #include "h264bsd_decoder.h"
  // AFTER:
  #include "esp_h264_decoder.h"
  ```

  Change line 48:
  ```cpp
  // BEFORE:
  H264BsdDecoder* _dec_handle = nullptr;
  // AFTER:
  EspH264Decoder* _dec_handle = nullptr;
  ```

  No changes to `video_stream.cpp` — `EspH264Decoder` has the identical public API.

- [ ] **Step 2: Delete old files**

  ```bash
  git -C .worktrees/feature-h264 rm \
    Dicemaster/h264bsd_decoder.h \
    Dicemaster/h264bsd_decoder.cpp \
    Dicemaster/esp_h264_stub.h
  ```

- [ ] **Step 3: Compile**

  Expected: no errors. `video_stream.cpp` references `_dec_handle->init()`, `_dec_handle->decode_frame()`, `_dec_handle->is_initialized()`, `_dec_handle->~EspH264Decoder()` — all present in `EspH264Decoder`.

- [ ] **Step 4: Commit**

  ```bash
  git -C .worktrees/feature-h264 add Dicemaster/video_stream.h
  git -C .worktrees/feature-h264 commit -m \
    "feat: wire EspH264Decoder into VideoStream, remove h264bsd files"
  ```

---

## Task 6: Add 2× Upscaling Helpers

**Files:**
- Modify: `Dicemaster/media.h`
- Modify: `Dicemaster/media.cpp`

- [ ] **Step 1: Add declarations to `Dicemaster/media.h`**

  In the `Image` class block (after the existing static methods, around line 77+), add:

  ```cpp
  // Expand a w×h RGB565 image 2× in each dimension into dst (must be ≥ 4*w*h bytes).
  // Uses 32-bit horizontal writes + row memcpy — 10–20× faster than a per-pixel loop.
  static void upscale_bmp565_2x(const uint16_t* src, uint16_t* dst, int w, int h);

  // Decode 240×240 I420 and 2× upscale to 480×480 RGB565 in one call.
  // Uses a 240×240×2 = 115KB PSRAM temp buffer allocated on each call.
  static void yuv420_to_rgb565_2x(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                   uint16_t* dst_480, int src_w, int src_h);
  ```

- [ ] **Step 2: Implement `upscale_bmp565_2x` in `Dicemaster/media.cpp`**

  Add after the `yuv420_to_rgb565()` implementation (which is in the `VideoFrame` section):

  ```cpp
  void Image::upscale_bmp565_2x(const uint16_t* src, uint16_t* dst, int w, int h) {
      uint16_t* out_row = dst;
      for (int y = 0; y < h; y++) {
          const uint16_t* src_row = src + y * w;
          uint16_t* even_row = out_row;
          // Expand each pixel horizontally into two adjacent pixels using 32-bit writes
          uint32_t* dst32 = reinterpret_cast<uint32_t*>(even_row);
          for (int x = 0; x < w; x++) {
              uint32_t p = src_row[x];
              dst32[x] = (p << 16) | p;  // write two copies in one 32-bit store
          }
          // Duplicate the expanded row below it
          memcpy(even_row + w * 2, even_row, w * 2 * sizeof(uint16_t));
          out_row += w * 4;  // advance 2 destination rows (each 2w wide)
      }
  }
  ```

- [ ] **Step 3: Implement `yuv420_to_rgb565_2x` in `Dicemaster/media.cpp`**

  ```cpp
  void Image::yuv420_to_rgb565_2x(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                   uint16_t* dst_480, int src_w, int src_h) {
      // Allocate temporary 240×240 RGB565 buffer in PSRAM
      uint16_t* tmp = static_cast<uint16_t*>(
          heap_caps_malloc(src_w * src_h * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
      if (!tmp) {
          Serial.println("[VIDEO] yuv420_to_rgb565_2x: PSRAM alloc failed, skipping frame");
          return;
      }
      VideoFrame::yuv420_to_rgb565(y, u, v, tmp, src_w, src_h);
      upscale_bmp565_2x(tmp, dst_480, src_w, src_h);
      heap_caps_free(tmp);
  }
  ```

- [ ] **Step 4: Optimize `JPEGDraw240` in `Dicemaster/media.cpp`**

  Replace the existing nested `for` loop (lines ~183–208 in media.cpp) with `upscale_bmp565_2x`. The callback receives one MCU row strip at a time; `pDraw->x`/`pDraw->y` gives the top-left position and `pDraw->iWidth`/`pDraw->iHeight` gives the strip dimensions.

  ```cpp
  int Image::JPEGDraw240(JPEGDRAW* pDraw) {
      Image* img = static_cast<Image*>(pDraw->pUser);
      if (img == nullptr || img->decoded_content == nullptr) return 0;

      img->decode_mtx.lock();

      int sw = pDraw->iWidth;
      int sh = pDraw->iHeight;
      int dst_x = pDraw->x * 2;   // destination origin in 480×480 space
      int dst_y = pDraw->y * 2;

      // Allocate a temporary 2× output strip for this MCU block
      uint16_t* expanded = static_cast<uint16_t*>(
          heap_caps_malloc(sw * 2 * sh * 2 * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
      if (expanded) {
          Image::upscale_bmp565_2x(
              reinterpret_cast<const uint16_t*>(pDraw->pPixels),
              expanded, sw, sh);

          // Write expanded block into decoded_content at the correct 480×480 position
          for (int row = 0; row < sh * 2; row++) {
              int dy = dst_y + row;
              if (dy < 0 || dy >= 480) continue;
              int copy_w = sw * 2;
              if (dst_x + copy_w > 480) copy_w = 480 - dst_x;
              if (copy_w <= 0) continue;
              memcpy(&img->decoded_content[dy * 480 + dst_x],
                     expanded + row * sw * 2,
                     copy_w * sizeof(uint16_t));
          }
          heap_caps_free(expanded);
      } else {
          // Fallback: original per-pixel path if PSRAM is low
          const uint16_t* src_line = reinterpret_cast<const uint16_t*>(pDraw->pPixels);
          for (int row = 0; row < sh; row++) {
              for (int col = 0; col < sw; col++) {
                  uint16_t pixel = src_line[row * sw + col];
                  int bx = (pDraw->x + col) * 2;
                  int by = (pDraw->y + row) * 2;
                  for (int dy = 0; dy < 2; dy++) {
                      for (int dx = 0; dx < 2; dx++) {
                          int dx2 = bx + dx, dy2 = by + dy;
                          if (dx2 >= 0 && dx2 < 480 && dy2 >= 0 && dy2 < 480)
                              img->decoded_content[dy2 * 480 + dx2] = pixel;
                      }
                  }
              }
          }
      }

      img->decode_mtx.unlock();
      return 1;
  }
  ```

- [ ] **Step 5: Compile**

  Sketch → Verify/Compile. Expected: no errors.

- [ ] **Step 6: Commit**

  ```bash
  git -C .worktrees/feature-h264 add Dicemaster/media.h Dicemaster/media.cpp
  git -C .worktrees/feature-h264 commit -m "feat: add upscale_bmp565_2x and optimize JPEGDraw240"
  ```

---

## Task 7: Add `Screen::draw_bmp565_2x`

**Files:**
- Modify: `Dicemaster/screen.h`
- Modify: `Dicemaster/screen.cpp`

- [ ] **Step 1: Declare in `screen.h`**

  In the public section of `Screen`, after `draw_startup_logo`:

  ```cpp
  // Draw a w×h RGB565 image upscaled 2× to fill the display, with rotation applied.
  // Allocates a 480×480 PSRAM temp buffer; w and h must both be ≤ 240.
  void draw_bmp565_2x(const uint16_t* src, int src_w, int src_h, Rotation rot);
  ```

- [ ] **Step 2: Implement in `screen.cpp`**

  Add at the end of screen.cpp:

  ```cpp
  void Screen::draw_bmp565_2x(const uint16_t* src, int src_w, int src_h, Rotation rot) {
      uint16_t* tmp = static_cast<uint16_t*>(
          ps_malloc(480 * 480 * sizeof(uint16_t)));
      if (!tmp) {
          Serial.println("[SCREEN] draw_bmp565_2x: ps_malloc failed");
          return;
      }
      Image::upscale_bmp565_2x(src, tmp, src_w, src_h);
      draw_bmp565_rotated(tmp, rot);
      free(tmp);
  }
  ```

- [ ] **Step 3: Compile**

  Sketch → Verify/Compile. Expected: no errors.

- [ ] **Step 4: Commit**

  ```bash
  git -C .worktrees/feature-h264 add Dicemaster/screen.h Dicemaster/screen.cpp
  git -C .worktrees/feature-h264 commit -m "feat: add Screen::draw_bmp565_2x API"
  ```

---

## Task 8: Generate Test Video

**Files:**
- Create: `scripts/gen_test_video.py`
- Create: `Dicemaster/test_video.h` (generated, then committed)

- [ ] **Step 1: Write `scripts/gen_test_video.py`**

  ```python
  #!/usr/bin/env python3
  """
  Generate a rotating U-M block-M logo video and embed it as a C header.
  
  Outputs:
    Dicemaster/test_video.h  — uint8_t TEST_VIDEO_DATA[] with per-frame size table
  
  Requirements: pip install Pillow; ffmpeg on PATH
  Run: python3 scripts/gen_test_video.py
  """
  
  import os, sys, subprocess, struct, shutil, tempfile
  from pathlib import Path
  from PIL import Image, ImageDraw, ImageFont
  
  SCRIPT_DIR = Path(__file__).parent
  REPO_ROOT  = SCRIPT_DIR.parent
  OUT_HEADER = REPO_ROOT / "Dicemaster" / "test_video.h"
  
  # Video parameters
  WIDTH  = 480
  HEIGHT = 480
  FPS    = 5
  FRAMES = 15   # 3 seconds @ 5 fps
  
  # U-M colors
  BG_COLOR   = (0, 39, 76)    # #00274C — Michigan Blue
  M_COLOR    = (255, 203, 5)  # #FFCB05 — Maize
  
  
  def draw_block_m(size: int) -> Image.Image:
      """Return a square PIL image of the block-M, size×size."""
      img = Image.new("RGB", (size, size), BG_COLOR)
      d   = ImageDraw.Draw(img)
      # Draw a simple bold "M" using polygon (block M approximation)
      m  = size * 0.15   # margin
      w  = size - 2 * m
      h  = size - 2 * m
      # Left leg, right leg, centre-V, and two diagonal lines
      leg_w = w * 0.18
      mid_x = size / 2
      top_y = m
      bot_y = m + h
      # Left column
      d.rectangle([m, top_y, m + leg_w, bot_y], fill=M_COLOR)
      # Right column
      d.rectangle([size - m - leg_w, top_y, size - m, bot_y], fill=M_COLOR)
      # Left diagonal (top-left to centre-bottom)
      d.polygon([
          (m,           top_y),
          (m + leg_w,   top_y),
          (mid_x,       m + h * 0.45),
          (mid_x - leg_w * 0.7, m + h * 0.45),
      ], fill=M_COLOR)
      # Right diagonal (top-right to centre-bottom)
      d.polygon([
          (size - m - leg_w, top_y),
          (size - m,         top_y),
          (mid_x + leg_w * 0.7, m + h * 0.45),
          (mid_x,            m + h * 0.45),
      ], fill=M_COLOR)
      return img
  
  
  def generate_frames(tmpdir: str) -> list[Path]:
      base  = draw_block_m(WIDTH)
      paths = []
      for i in range(FRAMES):
          angle = i * (360 / FRAMES)
          frame = base.rotate(angle, resample=Image.BICUBIC, expand=False)
          p = Path(tmpdir) / f"frame_{i:04d}.png"
          frame.save(p)
          paths.append(p)
      return paths
  
  
  def encode_h264(tmpdir: str, out_file: str) -> None:
      cmd = [
          "ffmpeg", "-y",
          "-framerate", str(FPS),
          "-i", os.path.join(tmpdir, "frame_%04d.png"),
          "-c:v", "libx264",
          "-profile:v", "baseline",
          "-level", "3.0",
          "-crf", "28",
          "-pix_fmt", "yuv420p",
          "-f", "h264",
          out_file,
      ]
      result = subprocess.run(cmd, capture_output=True, text=True)
      if result.returncode != 0:
          print("ffmpeg stderr:", result.stderr)
          sys.exit(1)
  
  
  def find_access_units(data: bytes) -> list[tuple[int, int]]:
      """Return list of (offset, length) for each H.264 access unit in Annex B stream."""
      AU_NAL_TYPES = {1, 5}  # non-IDR slice, IDR slice — each starts a new AU
      starts = []
  
      i = 0
      while i < len(data) - 3:
          if data[i:i+3] == b'\x00\x00\x01':
              sc_len = 3
          elif i < len(data) - 4 and data[i:i+4] == b'\x00\x00\x00\x01':
              sc_len = 4
          else:
              i += 1
              continue
          nalu_pos = i + sc_len
          if nalu_pos < len(data):
              nal_type = data[nalu_pos] & 0x1F
              if nal_type in AU_NAL_TYPES:
                  starts.append(i)
          i = nalu_pos + 1
  
      aus = []
      for idx, start in enumerate(starts):
          end = starts[idx + 1] if idx + 1 < len(starts) else len(data)
          aus.append((start, end - start))
      return aus
  
  
  def write_header(data: bytes, aus: list[tuple[int, int]]) -> None:
      lines = [
          "// Auto-generated by scripts/gen_test_video.py — do not edit manually.",
          "#pragma once",
          "#include <stdint.h>",
          f"static constexpr int TEST_VIDEO_FRAMES = {len(aus)};",
          f"static constexpr int TEST_VIDEO_WIDTH  = {WIDTH};",
          f"static constexpr int TEST_VIDEO_HEIGHT = {HEIGHT};",
          f"static constexpr int TEST_VIDEO_FPS    = {FPS};",
          "",
          "// Byte length of each access unit in TEST_VIDEO_DATA",
          f"static const uint32_t TEST_VIDEO_FRAME_SIZES[{len(aus)}] PROGMEM = {{",
      ]
      lines.append("    " + ", ".join(str(size) for _, size in aus) + ",")
      lines.append("};")
      lines.append("")
      lines.append("// Concatenated Annex B access units")
      lines.append(f"static const uint8_t TEST_VIDEO_DATA[{len(data)}] PROGMEM = {{")
      # Emit 16 bytes per line
      for i in range(0, len(data), 16):
          chunk = data[i:i+16]
          lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
      lines.append("};")
  
      OUT_HEADER.write_text("\n".join(lines) + "\n", encoding="utf-8")
      print(f"Written {OUT_HEADER}  ({len(data)} bytes, {len(aus)} access units)")
  
  
  def main():
      if not shutil.which("ffmpeg"):
          print("ERROR: ffmpeg not found on PATH. Install it first.")
          sys.exit(1)
  
      with tempfile.TemporaryDirectory() as tmpdir:
          print("Generating frames...")
          generate_frames(tmpdir)
  
          h264_file = os.path.join(tmpdir, "test_video.h264")
          print("Encoding H.264 Baseline...")
          encode_h264(tmpdir, h264_file)
  
          data = Path(h264_file).read_bytes()
          print(f"Encoded stream: {len(data)} bytes")
  
          aus = find_access_units(data)
          if not aus:
              print("ERROR: No access units found — check ffmpeg output.")
              sys.exit(1)
          print(f"Found {len(aus)} access unit(s)")
  
          write_header(data, aus)
  
  
  if __name__ == "__main__":
      main()
  ```

- [ ] **Step 2: Run the script**

  ```bash
  pip3 install Pillow  # if not installed
  python3 scripts/gen_test_video.py
  ```

  Expected output:
  ```
  Generating frames...
  Encoding H.264 Baseline...
  Encoded stream: XXXXX bytes
  Found 15 access unit(s)
  Written Dicemaster/test_video.h  (XXXXX bytes, 15 access units)
  ```

  If fewer than 15 access units are found, the keyframe detection heuristic missed some frames. Open `test_video.h` and verify `TEST_VIDEO_FRAMES` is reasonable (5–15). If it is 1 (all data in one AU), the video is likely all in one GOP starting with an IDR; this is fine for the test — just set `TEST_VIDEO_FRAMES = 1` manually and verify visually.

- [ ] **Step 3: Commit**

  ```bash
  git add scripts/gen_test_video.py
  git -C .worktrees/feature-h264 add Dicemaster/test_video.h
  git -C .worktrees/feature-h264 commit -m "feat: add gen_test_video.py script and generated test_video.h"
  ```

---

## Task 9: Add On-Device Video Test

**Files:**
- Modify: `Dicemaster/tests.h`

- [ ] **Step 1: Add `test_video_playback()` to `TestSuite` in `tests.h`**

  Add this new public method to the `TestSuite` class (insert after `test_error_handling()`):

  ```cpp
  /**
   * Decode and display the embedded rotating U-M logo video (test_video.h).
   * Bypasses VideoStream/SPI to directly exercise EspH264Decoder + screen pipeline.
   * Loops the video indefinitely until reset.
   */
  void test_video_playback() {
      Serial.println("=== Video Playback Test ===");
      Serial.printf("[VIDEO_TEST] %d frames, %dx%d, %d fps\n",
                    TEST_VIDEO_FRAMES, TEST_VIDEO_WIDTH, TEST_VIDEO_HEIGHT, TEST_VIDEO_FPS);

      EspH264Decoder decoder;
      if (!decoder.init()) {
          Serial.println("[VIDEO_TEST] Decoder init failed");
          return;
      }

      uint16_t* slot = static_cast<uint16_t*>(
          heap_caps_malloc(TEST_VIDEO_WIDTH * TEST_VIDEO_HEIGHT * sizeof(uint16_t),
                           MALLOC_CAP_SPIRAM));
      if (!slot) {
          Serial.println("[VIDEO_TEST] PSRAM alloc for frame slot failed");
          return;
      }

      const uint32_t frame_ms = 1000u / TEST_VIDEO_FPS;

      // Loop video until board is reset
      while (true) {
          size_t offset = 0;
          for (int i = 0; i < TEST_VIDEO_FRAMES; ++i) {
              uint32_t sz = TEST_VIDEO_FRAME_SIZES[i];
              const uint8_t* au = TEST_VIDEO_DATA + offset;
              offset += sz;

              unsigned long t0 = millis();

              bool ok = decoder.decode_frame(au, sz, slot,
                                             TEST_VIDEO_WIDTH, TEST_VIDEO_HEIGHT);
              if (ok) {
                  screen->draw_bmp565_2x(slot, TEST_VIDEO_WIDTH, TEST_VIDEO_HEIGHT,
                                         Rotation::ROT_0);
                  screen->update();
              } else {
                  Serial.printf("[VIDEO_TEST] Frame %d decode failed\n", i);
              }

              unsigned long elapsed = millis() - t0;
              Serial.printf("[VIDEO_TEST] Frame %d: %lu ms\n", i, elapsed);

              if (elapsed < frame_ms) delay(frame_ms - elapsed);
          }
          decoder.reset();  // reset decoder state between loops
      }

      heap_caps_free(slot);  // unreachable, but documents ownership
  }
  ```

- [ ] **Step 2: Add required includes at the top of `tests.h`**

  Add after existing includes:
  ```cpp
  #include "esp_h264_decoder.h"
  #include "test_video.h"
  ```

- [ ] **Step 3: Wire into `run_all_tests()`**

  In `run_all_tests()`, add at the end (after `test_error_handling()`):

  ```cpp
  test_video_playback();  // loops indefinitely — keep last
  ```

- [ ] **Step 4: Enable `TESTING` mode and flash**

  In `Dicemaster.ino` line 20, set:
  ```cpp
  SystemMode current_mode = SystemMode::TESTING;
  ```

  Flash. Expected on Serial:
  ```
  === Video Playback Test ===
  [VIDEO_TEST] 15 frames, 480x480, 5 fps
  [H264] Decoder initialised
  [VIDEO_TEST] Frame 0: XXX ms
  [VIDEO_TEST] Frame 1: XXX ms
  ...
  ```

  Expected on display: rotating block-M logo playing at ~5 fps.

  Frame decode time should be 100–400 ms. If > 200 ms per frame the display will lag; this is acceptable for a decode-performance test.

- [ ] **Step 5: Restore PRODUCTION mode**

  ```cpp
  SystemMode current_mode = SystemMode::PRODUCTION;
  ```

- [ ] **Step 6: Commit**

  ```bash
  git -C .worktrees/feature-h264 add Dicemaster/tests.h Dicemaster/Dicemaster.ino
  git -C .worktrees/feature-h264 commit -m "feat: add on-device H.264 video playback test"
  ```

---

## Self-Review

**Spec coverage:**
- [x] Core 3.x upgrade → Task 1
- [x] Remove ESP32DMASPI (already done pre-plan) → noted in spec, no task needed
- [x] Display: `esp_lcd` + `Arduino_Canvas` → Task 3
- [x] `espressif/esp_h264` component source → Task 2
- [x] `EspH264Decoder` class → Task 4
- [x] Wire into VideoStream, delete old files → Task 5
- [x] `upscale_bmp565_2x` helper → Task 6
- [x] `yuv420_to_rgb565_2x` helper → Task 6
- [x] Optimize `JPEGDraw240` → Task 6
- [x] `Screen::draw_bmp565_2x` → Task 7
- [x] `gen_test_video.py` script → Task 8
- [x] `test_video.h` generated header → Task 8
- [x] On-device test → Task 9
- [x] LVGL deferred → in spec, no task
