#pragma once
#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include <functional>
#include <vector>
#include "esp_h264_stub.h"
#include "constants.h"
#include "media.h"
#include "screen.h"

namespace dice {

// Frame buffer pool: 4 pre-allocated PSRAM slots of 480*480*2 bytes each.
// VideoStream owns the pool; VideoFrame destructors return slots via callback.
static constexpr int   VIDEO_POOL_SIZE    = 4;
static constexpr int   VIDEO_FRAME_WIDTH  = 480;
static constexpr int   VIDEO_FRAME_HEIGHT = 480;
static constexpr size_t VIDEO_FRAME_BYTES     = VIDEO_FRAME_WIDTH * VIDEO_FRAME_HEIGHT * 2;
static constexpr size_t MAX_FRAME_BUF_BYTES   = 512 * 1024;  // 512 KB cap on compressed frame data

class VideoStream {
public:
    explicit VideoStream(uint8_t stream_id, Screen* screen, uint8_t fps = 30);
    ~VideoStream();

    // Called by DecodingHandler to feed data
    bool init(uint16_t width, uint16_t height, uint8_t fps,
              Rotation rotation, const uint8_t* config, uint16_t config_len);
    bool push_chunk(const uint8_t* data, size_t len);
    bool finalize_frame(uint8_t frame_type, uint32_t pts);
    void end_stream();

    // Flush: abort decoding, drain screen queue, reset decoder
    void flush(Screen* screen);

    // Atomically update the rotation applied to subsequent frames
    void set_rotation(Rotation rot) { _pending_rotation.store(rot); }

    uint8_t get_stream_id() const { return _stream_id; }
    bool    is_active()     const { return _active; }

private:
    uint8_t   _stream_id;
    Screen*   _screen;
    bool      _active = false;

    // Decoder state (stub — replace with real esp_h264 handles)
    esp_h264_dec_handle_t _dec_handle = nullptr;
    uint16_t _width  = 480;
    uint16_t _height = 480;
    uint32_t _frame_duration_ms = 33;  // 1000 / fps

    // Rotation applied at next frame boundary
    std::atomic<Rotation> _pending_rotation;

    // Frame buffer pool
    uint16_t*    _pool_slots[VIDEO_POOL_SIZE] = {};
    bool         _pool_used[VIDEO_POOL_SIZE]  = {};
    portMUX_TYPE _pool_mux = portMUX_INITIALIZER_UNLOCKED;

    // Accumulation buffer for the current frame's compressed data
    std::vector<uint8_t> _frame_buf;

    // Acquire/release pool slots
    uint16_t* acquire_slot();
    void      release_slot(uint16_t* ptr);

    void reset_decoder();
};

} // namespace dice
