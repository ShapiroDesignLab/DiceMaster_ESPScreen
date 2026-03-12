#include "video_stream.h"
#include "Arduino.h"
#include "esp_heap_caps.h"

namespace dice {

VideoStream::VideoStream(uint8_t stream_id, Screen* screen, uint8_t fps)
    : _stream_id(stream_id), _screen(screen),
      _frame_duration_ms(fps > 0 ? 1000u / fps : 33u),
      _pending_rotation(Rotation::ROT_0)
{
    // Allocate pool slots in PSRAM
    for (int i = 0; i < VIDEO_POOL_SIZE; ++i) {
        _pool_slots[i] = static_cast<uint16_t*>(
            heap_caps_malloc(VIDEO_FRAME_BYTES, MALLOC_CAP_SPIRAM));
        _pool_used[i] = false;
        if (!_pool_slots[i]) {
            Serial.printf("[VIDEO] Stream %d: PSRAM alloc failed for pool slot %d\n", stream_id, i);
        }
    }
}

VideoStream::~VideoStream() {
    reset_decoder();
    for (int i = 0; i < VIDEO_POOL_SIZE; ++i) {
        heap_caps_free(_pool_slots[i]);
        _pool_slots[i] = nullptr;
    }
}

bool VideoStream::init(uint16_t width, uint16_t height, uint8_t fps,
                       Rotation rotation, const uint8_t* /*config*/, uint16_t /*config_len*/) {
    _width  = width;
    _height = height;
    _frame_duration_ms = fps > 0 ? 1000u / fps : 33u;
    _pending_rotation.store(rotation);
    _active = true;
    Serial.printf("[VIDEO] Stream %d init %dx%d @ %d fps\n", _stream_id, width, height, fps);
    return true;
}

bool VideoStream::push_chunk(const uint8_t* data, size_t len) {
    if (!_active) return false;
    if (_frame_buf.size() + len > MAX_FRAME_BUF_BYTES) {
        Serial.printf("[VIDEO] Stream %d: frame buf overflow (%u bytes), ending stream\n",
                      _stream_id, (unsigned)(_frame_buf.size() + len));
        end_stream();
        return false;
    }
    _frame_buf.insert(_frame_buf.end(), data, data + len);
    return true;
}

bool VideoStream::finalize_frame(uint8_t /*frame_type*/, uint32_t /*pts*/) {
    if (!_active || _frame_buf.empty()) return false;

    uint16_t* slot = acquire_slot();
    if (!slot) {
        Serial.printf("[VIDEO] Stream %d: no pool slot available, dropping frame\n", _stream_id);
        _frame_buf.clear();
        return false;
    }

    // TODO: decode _frame_buf into slot via esp_h264_dec_process when real library available
    // For now, fill with a solid colour as a placeholder
    uint16_t colour = 0x07E0; // green in RGB565
    for (size_t px = 0; px < VIDEO_FRAME_WIDTH * (size_t)VIDEO_FRAME_HEIGHT; ++px) {
        slot[px] = colour;
    }

    Rotation rot = _pending_rotation.load();
    // NOTE: release_cb captures 'this'. VideoStream must outlive all VideoFrame objects
    // it creates. Always call flush() before destroying a VideoStream.
    auto release_cb = [this](uint16_t* ptr) { release_slot(ptr); };
    auto* frame = new VideoFrame(_stream_id, slot, _frame_duration_ms, rot, release_cb);

    if (!_screen->enqueue(frame)) {
        delete frame;  // returns slot via destructor
    }

    _frame_buf.clear();
    return true;
}

void VideoStream::end_stream() {
    _active = false;
    _frame_buf.clear();
    Serial.printf("[VIDEO] Stream %d ended\n", _stream_id);
}

void VideoStream::flush(Screen* screen) {
    _active = false;
    _frame_buf.clear();
    if (screen) screen->flush_video_stream(_stream_id);
    reset_decoder();
    Serial.printf("[VIDEO] Stream %d flushed\n", _stream_id);
}

uint16_t* VideoStream::acquire_slot() {
    uint16_t* result = nullptr;
    taskENTER_CRITICAL(&_pool_mux);
    for (int i = 0; i < VIDEO_POOL_SIZE; ++i) {
        if (!_pool_used[i] && _pool_slots[i]) {
            _pool_used[i] = true;
            result = _pool_slots[i];
            break;
        }
    }
    taskEXIT_CRITICAL(&_pool_mux);
    return result;
}

void VideoStream::release_slot(uint16_t* ptr) {
    taskENTER_CRITICAL(&_pool_mux);
    for (int i = 0; i < VIDEO_POOL_SIZE; ++i) {
        if (_pool_slots[i] == ptr) {
            _pool_used[i] = false;
            break;
        }
    }
    taskEXIT_CRITICAL(&_pool_mux);
}

void VideoStream::reset_decoder() {
    if (_dec_handle) {
        esp_h264_dec_close(_dec_handle);
        _dec_handle = nullptr;
    }
}

} // namespace dice
