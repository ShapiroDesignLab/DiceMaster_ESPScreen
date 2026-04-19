# H.264 Video Support Plan

## Problem Statement

The current GIF-style animation approach sends one JPEG per frame (effectively MJPEG). This saturates SPI bandwidth while leaving the decoding CPU mostly idle, because JPEG is intra-frame only — each frame is compressed independently with no knowledge of adjacent frames. Real video codecs exploit temporal redundancy (difference-between-frames), which dramatically reduces bandwidth, especially for low-motion content.

**Goals:**
1. Reduce SPI bandwidth requirements by 10–30× for video playback
2. Support rotation changes mid-playback
3. Support interrupt/abort packets that cleanly flush the decoding pipeline without memory leaks

---

## Research Findings

### Codec Selection: `esp_h264` (Espressif H.264 Component)

**Source:** [IDF Component Registry](https://components.espressif.com/components/espressif/esp_h264) · [GitHub](https://github.com/espressif/esp-h264-component) · [Usage Guide](https://developer.espressif.com/blog/2025/07/esp-h264-use-tips/)

The Espressif `esp_h264` component (tinyH264-based) is the correct choice:

| Property | Value |
|---|---|
| Platform | ESP32-S3 software decode, SIMD-accelerated |
| Profile | H.264 Baseline Profile |
| Output format | I420 (YUV420 planar); also supports YUYV |
| PSRAM at 640×480 | 2.5 MB (documented) |
| PSRAM at 480×480 | ~1.6 MB (interpolated) |
| Estimated fps at 480×480 | ~35–40 fps (better than documented 25–31 fps at 640×480) |
| Decoder statefulness | **Stateful** — must persist across frames (holds reference frame buffers) |
| NAL unit feeding | Annex B format (start code prefixed) or NALU size format |

**Why not other options:**
- **MPEG-1 (pl_mpeg / ESPFLIX)**: Patent-free and proven on ESP32, but ~2–3× worse compression than H.264 for equivalent quality.
- **Custom delta codec**: No existing library; would require implementing both encoder (master side) and decoder (ESP32 side). Not worth the effort when `esp_h264` exists.
- **VP8 / Theora**: No known production ESP32 implementations.
- **Adaptive JPEG**: Still intra-frame only; does not address the fundamental bandwidth problem.

### Confirmed API (from `esp_h264_dec.h`)

```c
// Initialize
esp_h264_dec_handle_t decoder;
esp_h264_dec_cfg_t cfg = { .width = 480, .height = 480, .fmt = ESP_H264_RAW_FMT_I420 };
esp_h264_dec_sw_new(&cfg, &decoder);
esp_h264_dec_open(decoder);

// Per-NAL-unit decode call
esp_h264_dec_in_frame_t  in  = { .raw_data = { .buffer = nal_bytes, .len = nal_len } };
esp_h264_dec_out_frame_t out = {};
esp_h264_err_t err = esp_h264_dec_process(decoder, &in, &out);
// out.out_size == 0 for SPS/PPS NALs (no image output)
// out.out_size > 0 for picture NALs; out.outbuf points to I420 data in decoder PSRAM

// Teardown
esp_h264_dec_close(decoder);
```

**I420 buffer layout** for 480×480:
```
Y plane:  480×480 bytes  (230,400 B)
U plane:  240×240 bytes  ( 57,600 B)  — starts at outbuf + 230400
V plane:  240×240 bytes  ( 57,600 B)  — starts at outbuf + 288000
Total:    345,600 bytes  (~345 KB)
```

### Bandwidth Analysis

**Current MJPEG approach:**
- 480×480 JPEG at quality ~80: 30–80 KB per frame
- At 30 fps: 0.9–2.4 MB/s = 7.2–19.2 Mbps
- Saturates available SPI bandwidth

**H.264 Baseline approach:**
- I-frames (keyframes, every ~30 frames): ~8–15 KB
- P-frames (inter-predicted, low motion): 0.5–5 KB typical
- At 30 fps with GOP size 30: average ~1–5 KB per frame = 0.03–0.15 MB/s
- **10–30× bandwidth reduction vs MJPEG**

---

## Architecture Design

### Overview: What Changes

The fundamental difference from JPEG decoding is that **H.264 is stateful** — the decoder maintains reference frames between calls and cannot be recreated per-frame. This requires a persistent `VideoStream` object that lives for the duration of a video playback session, separate from the per-frame `MediaContainer` objects that already exist.

The rest of the pipeline (SPI → DecodingHandler → Screen queue → render) is unchanged. The new components plug in at the `DecodingHandler` layer.

```
SPI Slave ──→ DecodingHandler
                 ├── existing: TextBatch → TextGroup → Screen queue
                 ├── existing: ImageStart/Chunk → Image → Screen queue
                 └── NEW: VideoFrameStart/Chunk → VideoStream (stateful)
                                                      ├── esp_h264_dec_process()
                                                      ├── YUV→RGB565 convert
                                                      └── VideoFrame → Screen queue
                                                                          └── draw_bmp565_rotated() (unchanged)
```

---

## New Protocol Message Types

Extend `MessageType` in `constants.h` starting at `0x10` (after existing `0x0F = ERROR`):

```cpp
enum class MessageType : uint8_t {
    // ... existing 0x01–0x0F unchanged ...
    VIDEO_STREAM_INIT  = 0x10,  // Configure H.264 stream; delivers SPS+PPS NAL units
    VIDEO_FRAME_START  = 0x11,  // Start of one H.264 access unit, embedded chunk 0 included
    VIDEO_FRAME_CHUNK  = 0x12,  // Continuation chunk for the current frame
    VIDEO_STREAM_END   = 0x13,  // Graceful end-of-stream
    VIDEO_FLUSH        = 0x14,  // Emergency abort: drain pipeline immediately, free all state
    SET_ROTATION       = 0x15,  // Change display rotation; takes effect on next decoded frame
};
```

Also extend `MediaType`:
```cpp
enum class MediaType : uint8_t {
    TEXT      = 0,
    TEXTGROUP = 1,
    IMAGE     = 2,
    GIF       = 5,
    VIDEO     = 6,   // decoded video frame (VideoFrame MediaContainer)
    CTRL      = 255
};
```

---

## Protocol Payload Structs

Add to `protocol.h` inside `namespace DProtocol`:

```cpp
// ─── VIDEO_STREAM_INIT payload ─────────────────────────────────────────────
// Sent once before any frames. Delivers codec config (SPS+PPS) so the decoder
// can be initialized. Header is fixed-size; config bytes trail variable-length.
struct VideoStreamInit {
    uint8_t  stream_id;      // 0–7; identifies this logical stream
    uint8_t  codec;          // 0x01 = H.264 Baseline Profile
    uint16_t width;          // Encoded frame width  (e.g. 480)
    uint16_t height;         // Encoded frame height (e.g. 480)
    uint8_t  fps;            // Target playback fps
    uint8_t  rotation;       // Initial display Rotation value (0–3)
    uint8_t  gop_size;       // Frames between I-frames (keyframe interval)
    uint16_t config_len;     // Byte length of SPS+PPS data that immediately follows
    // Followed by: config_len bytes of SPS+PPS in Annex-B format (with start codes)
};

// ─── VIDEO_FRAME_START payload ─────────────────────────────────────────────
// Mirrors IMAGE_TRANSFER_START. Embedded chunk 0 allows small frames
// (e.g. tiny P-frames) to be sent in a single SPI transaction.
struct VideoFrameStart {
    uint8_t  stream_id;
    uint8_t  frame_type;     // 0x00 = P-frame, 0x01 = I-frame (IDR)
    uint32_t pts;            // Presentation timestamp in ms (for pacing / drop detection)
    uint32_t total_size;     // Total compressed NAL bytes for this frame (3 bytes on wire)
    uint8_t  num_chunks;     // Total chunks including embedded chunk 0
    ImageChunk embedded_chunk0;  // Re-uses existing ImageChunk struct (chunk_id = 0)
    // (chunk_id = 0; stream_id occupies the imgId slot of ImageChunk)
};

// ─── VIDEO_FRAME_CHUNK payload ─────────────────────────────────────────────
// Identical wire format to ImageChunk. The stream_id occupies the imgId byte.
// Re-use ImageChunk struct directly; no new struct needed.
//   stream_id[1]  chunk_id[1]  offset[3]  length[2]  data[N]

// ─── VIDEO_STREAM_END payload ──────────────────────────────────────────────
struct VideoStreamEnd {
    uint8_t stream_id;
    uint8_t reason;          // 0x00 = normal EOS, 0x01 = loop, 0x02 = interrupted
};

// ─── VIDEO_FLUSH payload ───────────────────────────────────────────────────
// Instructs the ESP32 to immediately abort the named stream (or all streams),
// drain queued frames from the Screen queue, and reset the decoder state.
struct VideoFlush {
    uint8_t stream_id;       // 0xFF = flush ALL active streams
    uint8_t reason;          // 0x00 = preempted, 0x01 = error, 0x02 = rotation change
};

// ─── SET_ROTATION payload ──────────────────────────────────────────────────
// Updates the display rotation. Takes effect on the next decoded frame.
// target = 0x00 also updates DEFAULT_ROTATION, affecting text and static images.
struct SetRotation {
    uint8_t target;          // 0x00 = global (all media), 0x01 = one specific stream
    uint8_t stream_id;       // Only used when target = 0x01
    uint8_t rotation;        // Rotation enum value (0–3)
};
```

Corresponding `PayloadTag` additions:
```cpp
enum PayloadTag : uint8_t {
    // ... existing tags ...
    TAG_VIDEO_STREAM_INIT,
    TAG_VIDEO_FRAME_START,
    TAG_VIDEO_FRAME_CHUNK,
    TAG_VIDEO_STREAM_END,
    TAG_VIDEO_FLUSH,
    TAG_SET_ROTATION,
};
```

---

## New Class: `VideoFrame` (`media.h`)

A leaf `MediaContainer` that holds one decoded, RGB565-converted frame. It is **immediately `READY`** upon construction — no async decode step — because `VideoStream` performs the H.264 decode synchronously before creating this object.

The `stream_id` field enables `Screen::flush_video_stream()` to selectively drain only frames belonging to the aborted stream.

```cpp
// media.h — add inside namespace dice:

class VideoFrame : public MediaContainer {
private:
    const uint8_t  stream_id;
    uint16_t*      rgb565;       // PSRAM: 480×480×2 = ~461 KB; NOT freed in destructor
    const Rotation rotation;     // Rotation captured at decode time

    // Callback invoked in destructor to return the PSRAM slot to VideoStream's pool.
    // This keeps memory ownership clear: pool owns the buffer, VideoFrame borrows it.
    std::function<void(uint16_t*)> slot_release_cb;

public:
    VideoFrame(uint8_t sid, uint16_t* rgb_data, size_t display_duration_ms,
               Rotation rot, std::function<void(uint16_t*)> release_cb)
        : MediaContainer(MediaType::VIDEO, display_duration_ms)
        , stream_id(sid), rgb565(rgb_data), rotation(rot)
        , slot_release_cb(std::move(release_cb))
    { set_status(MediaStatus::READY); }

    ~VideoFrame() override {
        // Return the PSRAM slot to the pool — never free the pointer directly.
        if (slot_release_cb) slot_release_cb(rgb565);
    }

    // MediaContainer overrides
    uint8_t   get_stream_id()  const          { return stream_id; }
    uint16_t* get_img()        override        { return rgb565; }
    Rotation  get_rotation()   const override  { return rotation; }
    uint8_t   get_image_id()   override        { return stream_id; }

    // Static helper: convert I420 (YUV420 planar) → RGB565 into a pre-allocated dst.
    // dst must be PSRAM-allocated, width×height×2 bytes.
    static void yuv420_to_rgb565(const uint8_t* y_plane,
                                  const uint8_t* u_plane,
                                  const uint8_t* v_plane,
                                  uint16_t* dst, int width, int height);
};
```

`MediaContainer` base class needs one new virtual method (defaults to 0xFF = "not a video frame"):
```cpp
virtual uint8_t get_stream_id() const { return 0xFF; }
```

---

## New Class: `VideoStream` (`video_stream.h`)

The core new file. Wraps the H.264 decoder state that must **persist across frames**. Handles frame assembly (chunked, mirroring `DecodingHandler`'s image logic), decode, YUV→RGB565 conversion, frame pool management, and flush.

```cpp
// Dicemaster/video_stream.h
#pragma once

#include <atomic>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_h264_dec.h"   // Espressif H.264 component
#include "media.h"
#include "screen.h"
#include "constants.h"
#include "protocol.h"

namespace dice {

// Number of pre-allocated RGB565 frame slots in the pool.
// Caps PSRAM usage: 4 × (480×480×2) = ~1.84 MB.
// Provides ~133 ms of decode-ahead buffer at 30 fps.
// Decoder blocks (via semaphore) if all slots are in use, providing back-pressure.
constexpr size_t VIDEO_FRAME_POOL_SIZE = 4;

class VideoStream {
public:
    // ─── Construction / Destruction ──────────────────────────────────────────
    VideoStream(uint8_t sid, uint16_t width, uint16_t height,
                uint8_t fps, Rotation initial_rotation, Screen* screen);
    ~VideoStream();

    // ─── Configuration (called on VIDEO_STREAM_INIT) ─────────────────────────
    // Feed Annex-B SPS+PPS bytes to initialize the H.264 decoder.
    // Must be called before any VIDEO_FRAME_START messages.
    bool configure(const uint8_t* sps_pps_annex_b, size_t len);

    // ─── Frame assembly (called from DecodingHandler::processing_task) ───────
    // Begin receiving a new compressed frame.
    // chunk0_data / chunk0_len: embedded first chunk from VIDEO_FRAME_START.
    // Returns false if flush_pending or parameters invalid.
    bool start_frame(uint8_t frame_type, uint32_t pts,
                     uint32_t total_nal_bytes, uint8_t num_chunks,
                     const uint8_t* chunk0_data, uint16_t chunk0_len);

    // Append a continuation chunk (VIDEO_FRAME_CHUNK).
    // Returns false if flush_pending or chunk out of order.
    bool add_frame_chunk(uint8_t chunk_id, uint32_t byte_offset,
                         const uint8_t* data, uint16_t len);

    // ─── Rotation (lock-free, called on SET_ROTATION) ─────────────────────────
    // Atomically updates pending_rotation.
    // Takes effect on the NEXT frame enqueued to Screen — no re-decode needed.
    void set_rotation(Rotation rot) { pending_rotation.store(rot); }

    // ─── Flush (called on VIDEO_FLUSH) ────────────────────────────────────────
    // Three-phase operation — see "Interrupt / Flush Design" section below.
    // Returns number of VideoFrame objects freed from the Screen queue.
    int flush(Screen* screen);

    // ─── Accessors ────────────────────────────────────────────────────────────
    uint8_t get_stream_id()   const { return stream_id; }
    bool    is_initialized()  const { return decoder_ready; }
    bool    is_flushing()     const { return flush_pending.load(); }

private:
    // ─── Identity ─────────────────────────────────────────────────────────────
    const uint8_t  stream_id;
    const uint16_t enc_width, enc_height;
    const uint8_t  target_fps;
    Screen*        screen_ref;

    // ─── Rotation ─────────────────────────────────────────────────────────────
    // std::atomic so SET_ROTATION from DecodingHandler does not race with decode.
    std::atomic<Rotation> pending_rotation;

    // ─── H.264 decoder ────────────────────────────────────────────────────────
    esp_h264_dec_handle_t decoder;
    bool                  decoder_ready;

    // Destroy decoder context and recreate clean; used during flush.
    void reset_decoder();

    // ─── NAL receive buffer ───────────────────────────────────────────────────
    // Accumulates compressed bytes for the current frame being assembled.
    // Mirrors the Image class's content/input_ptr pattern.
    uint8_t*  nal_buf;               // PSRAM
    size_t    nal_buf_capacity;      // allocated size
    size_t    nal_written;           // bytes filled
    uint8_t   nal_num_chunks;
    uint8_t   nal_chunks_received;
    uint8_t   nal_frame_type;
    uint32_t  nal_pts;

    // ─── Frame buffer pool ────────────────────────────────────────────────────
    // Pre-allocated PSRAM slots; each holds one decoded RGB565 frame.
    // Owned by VideoStream; borrowed by VideoFrame while in Screen queue.
    struct FrameSlot {
        uint16_t* rgb565;   // PSRAM, enc_width × enc_height × 2 bytes
        bool      in_use;
    };
    FrameSlot         pool[VIDEO_FRAME_POOL_SIZE];
    SemaphoreHandle_t pool_sem;  // counting semaphore; value = number of free slots

    FrameSlot* acquire_slot();                  // blocks until a slot is free
    void       release_slot(uint16_t* rgb565);  // called via VideoFrame destructor callback

    // ─── Flush flag ───────────────────────────────────────────────────────────
    // Set atomically by flush(); checked at entry of start_frame() and add_frame_chunk().
    std::atomic<bool> flush_pending;

    // ─── YUV intermediate buffer ──────────────────────────────────────────────
    // Reused across frames to avoid per-frame allocation.
    // Size: enc_width × enc_height × 3/2 bytes (~345 KB for 480×480).
    uint8_t* yuv_buf;  // PSRAM

    // ─── Decode ───────────────────────────────────────────────────────────────
    // Called when nal_chunks_received == nal_num_chunks.
    // Runs: esp_h264_dec_process() → yuv420_to_rgb565() → VideoFrame → Screen::enqueue()
    bool decode_complete_frame();
};

} // namespace dice
```

### `decode_complete_frame()` Sketch

```cpp
bool VideoStream::decode_complete_frame() {
    esp_h264_dec_in_frame_t  in  = { .raw_data = { .buffer = nal_buf,
                                                    .len    = nal_written } };
    esp_h264_dec_out_frame_t out = {};
    esp_h264_err_t err = esp_h264_dec_process(decoder, &in, &out);

    if (err != ESP_H264_ERR_OK) {
        Serial.println("[VIDEO] Decode error: " + String(err));
        return false;
    }
    if (out.out_size == 0) {
        // SPS/PPS NAL — no image output, this is normal
        return true;
    }

    // out.outbuf: I420 layout — Y, then U, then V
    uint8_t* y = out.outbuf;
    uint8_t* u = out.outbuf + enc_width * enc_height;
    uint8_t* v = out.outbuf + enc_width * enc_height * 5 / 4;

    // Acquire a PSRAM frame slot (blocks if all 4 are in Screen queue)
    FrameSlot* slot = acquire_slot();
    VideoFrame::yuv420_to_rgb565(y, u, v, slot->rgb565, enc_width, enc_height);

    Rotation rot = pending_rotation.load();
    size_t   duration_ms = 1000 / target_fps;

    auto* frame = new VideoFrame(
        stream_id, slot->rgb565, duration_ms, rot,
        [this](uint16_t* p){ release_slot(p); }
    );
    if (!screen_ref->enqueue(frame)) {
        delete frame;  // destructor calls release_slot() — no leak
        return false;
    }
    return true;
}
```

---

## Rotation Mid-Playback

### How it works

```
Master sends SET_ROTATION (rotation = ROT_90)
    │
    ▼
DecodingHandler::handle(SetRotation)
    calls stream->set_rotation(ROT_90)   ← atomic store, no lock, no flush
    │
    ▼ (next time decode_complete_frame() runs)
    Rotation rot = pending_rotation.load()   → ROT_90
    VideoFrame constructed with rot = ROT_90
    │
    ▼
Screen::draw_img(VideoFrame*)
    calls draw_bmp565_rotated(rgb_ptr, ROT_90)
    → existing pixel-copy transform, no new code needed
```

**Zero decoder disruption.** Rotation takes effect on the very next decoded frame with sub-one-frame latency. No re-decode, no NAL discarding, no decoder state reset.

### If instant rotation with no stale frames is required

Send `VIDEO_FLUSH` immediately after `SET_ROTATION`:
1. `SET_ROTATION` updates the atomic rotation (instant)
2. `VIDEO_FLUSH` drains already-queued frames (all had old rotation)
3. Next frame decoded after flush carries the new rotation

### Global rotation (`target = 0x00`)

`SET_ROTATION` with `target = 0x00` should update the `DEFAULT_ROTATION` global in `Dicemaster.ino` in addition to updating any active video stream. This affects subsequent `TextGroup` and static `Image` media as well.

---

## Interrupt / Flush Design

### Three-phase flush

The flush is executed entirely within the `DecodingHandler::processing_task` thread, so there is no race between producing new frames and flushing.

**Phase 1 — Stop the producer (instant)**

```cpp
// DecodingHandler::handle(VideoFlush):
stream->flush_pending.store(true, std::memory_order_release);
```

After this store, any concurrent or subsequent `start_frame()` / `add_frame_chunk()` call checks the flag at entry and returns `false` immediately. The partially-assembled frame in `nal_buf` is abandoned (freed in Phase 3).

**Phase 2 — Drain the Screen queue**

```cpp
// Screen::flush_video_stream(uint8_t target_stream_id):
void Screen::flush_video_stream(uint8_t target_stream_id) {
    if (xSemaphoreTake(queue_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    size_t depth = uxQueueMessagesWaiting(media_queue);
    std::vector<MediaContainer*> survivors;
    survivors.reserve(depth);

    MediaContainer* item;
    while (xQueueReceive(media_queue, &item, 0) == pdTRUE && item) {
        bool is_target =
            (item->get_media_type() == MediaType::VIDEO) &&
            (target_stream_id == 0xFF ||
             item->get_stream_id() == target_stream_id);
        if (is_target) {
            delete item;   // VideoFrame destructor calls slot_release_cb
                           // → release_slot() → xSemaphoreGive(pool_sem)
                           // → slot is immediately available for reuse
        } else {
            survivors.push_back(item);
        }
    }
    for (auto* s : survivors)
        xQueueSend(media_queue, &s, 0);

    xSemaphoreGive(queue_mutex);
}
```

**Phase 3 — Reset decoder state**

```cpp
// VideoStream::flush():
free(nal_buf);           // drop in-progress compressed frame data
nal_buf        = nullptr;
nal_written    = 0;

esp_h264_dec_close(decoder);   // free ~1.6 MB decoder context
esp_h264_dec_sw_new(&cfg, &decoder);
esp_h264_dec_open(decoder);    // fresh decoder, ready for new VIDEO_STREAM_INIT

flush_pending.store(false, std::memory_order_release);
```

### Memory safety invariants

| Resource | Owner | Freed when |
|---|---|---|
| `nal_buf` (PSRAM) | `VideoStream` | `flush()` Phase 3, or `~VideoStream()` |
| `yuv_buf` (PSRAM) | `VideoStream` | `~VideoStream()` |
| `pool[i].rgb565` (PSRAM) | `VideoStream` | `~VideoStream()` |
| `VideoFrame` heap object | `Screen` media queue | `Screen::flush_video_stream()` or `display_next()` after expiry |
| Pool slot reference in `VideoFrame` | Borrowed | Returned in `VideoFrame::~VideoFrame()` via callback |
| H.264 decoder context | `VideoStream` | `flush()` Phase 3 (reset) or `~VideoStream()` |

No resource can be double-freed:
- Each pool slot has a `bool in_use` flag; `acquire_slot()` sets it, `release_slot()` clears it.
- `VideoFrame::~VideoFrame()` always calls the release callback (it cannot be bypassed).
- `Screen::flush_video_stream()` deletes `VideoFrame` objects, which fires the destructor path above.

---

## Changes Required to Existing Files

| File | Change |
|---|---|
| `constants.h` | Add `MessageType` 0x10–0x15; add `MediaType::VIDEO = 6` |
| `protocol.h` | Add 6 new payload structs; add `PayloadTag` entries; add encode/decode functions |
| `media.h` | Add `VideoFrame` class; add `virtual uint8_t get_stream_id() const { return 0xFF; }` to `MediaContainer` base |
| `media.cpp` | Implement `VideoFrame::yuv420_to_rgb565()` |
| `screen.h` | Add `flush_video_stream(uint8_t stream_id)` declaration; allow `MediaType::VIDEO` in `enqueue()` |
| `screen.cpp` | Implement `flush_video_stream()`; add `case MediaType::VIDEO` to `display_next()` dispatch (routes to `draw_img()`, which already works since `VideoFrame` provides `get_img()` and `get_rotation()`) |
| `decoding_handler.h` | Add `#include "video_stream.h"`; add `std::map<uint8_t, VideoStream*> active_video_streams`; add 6 handler methods |

### New files

| File | Contents |
|---|---|
| `Dicemaster/video_stream.h` | `VideoStream` class (the largest new component) |

---

## Memory Budget

| Component | PSRAM |
|---|---|
| H.264 decoder context (480×480) | ~1.6 MB |
| NAL receive buffer (one in-flight frame, max) | ~100 KB |
| YUV intermediate buffer (reused each frame) | ~345 KB |
| Frame buffer pool (4 × 480×480×2 RGB565) | ~1.84 MB |
| Existing JPEG image support (unchanged) | ~920 KB |
| Screen pixel buffer (`screen_buffer`) | ~461 KB |
| **Total** | **~5.3 MB** |
| **Available headroom (8 MB board)** | **~2.7 MB** |

The `VIDEO_FRAME_POOL_SIZE = 4` constant in `video_stream.h` is the primary knob for trading PSRAM against decode-ahead buffer depth. Reduce to `2` for tighter memory, increase to `6` if smoother playback is needed (as long as PSRAM headroom allows).

---

## Implementation Order

1. **Add `esp_h264` dependency** — add to `idf_component.yml` (ESP-IDF builds) or locate the Arduino library wrapper if building under Arduino IDE. Verify it links against the S3 SIMD-accelerated backend.

2. **`constants.h`** — add `MessageType` 0x10–0x15 and `MediaType::VIDEO`.

3. **`media.h` / `media.cpp`** — add `get_stream_id()` virtual to `MediaContainer`; add `VideoFrame` class and `yuv420_to_rgb565()`.

4. **`screen.h` / `screen.cpp`** — add `flush_video_stream()`; allow `VIDEO` type through `enqueue()`; add `VIDEO` case in `display_next()`.

5. **`protocol.h`** — add 6 payload structs, `PayloadTag` entries, and encode/decode functions.

6. **`video_stream.h`** — implement `VideoStream` in full (the largest piece of work).

7. **`decoding_handler.h`** — wire in `active_video_streams` map and the 6 new message handlers.

8. **Python test harness** — a script that reads a short MP4 or GIF, encodes it as H.264 Baseline using `ffmpeg` or `av`, packetizes it into the new SPI protocol message format, and sends it over USB-serial or SPI to verify end-to-end decode and display.

---

## Open Questions / Risks

- **`esp_h264` under Arduino IDE**: The component is designed for ESP-IDF builds. It may require the Arduino-ESP32 IDF component integration or a manual port. Confirm this before starting implementation.
- **B-frames**: `esp_h264` Baseline Profile does not support B-frames, which is correct for our use case (low latency, no bidirectional prediction needed).
- **Encoder side**: The master controller (not this repo) must encode video as H.264 Baseline Profile, Annex B byte stream format. FFmpeg command: `ffmpeg -i input.mp4 -vcodec libx264 -profile:v baseline -level 3.0 -x264-params "nal-hrd=cbr:force-cfr=1" -b:v 500k output.h264`
- **480×480 non-standard resolution**: H.264 Baseline requires width and height to be multiples of 16 (macroblock size). 480 = 30 × 16 ✓ — this is fine.
- **SPS/PPS delivery**: The `VIDEO_STREAM_INIT` message must be sent every time playback starts (including after a `VIDEO_FLUSH`), since the decoder is reset. The master should always send `VIDEO_STREAM_INIT` before the first frame of any new clip or loop.
