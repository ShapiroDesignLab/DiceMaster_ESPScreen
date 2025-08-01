#include "media.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"

namespace dice {

void MediaContainer::set_status(MediaStatus new_status) {
    std::lock_guard<std::mutex> lock(status_mtx);
    status = new_status;
}

MediaContainer::MediaContainer(MediaType med_type, size_t dur)
    : media_type(med_type)
    , status(MediaStatus::NOT_RECEIVED)
    , duration(dur)
    , start_time(0) 
    {}

MediaContainer::~MediaContainer() {}

MediaType MediaContainer::get_media_type() const {
    return media_type;
}

MediaStatus MediaContainer::get_status() {
    bool expired = (start_time > 0 && (millis() - start_time) >= duration);
    if (expired) {
        set_status(MediaStatus::EXPIRED);
    }
    std::lock_guard<std::mutex> lock(status_mtx);
    return status;
}

void MediaContainer::trigger_display() {
    if (start_time != 0) return;   // Only trigger once
    set_status(MediaStatus::DISPLAYING);
    start_time = max(millis(), (long unsigned int) 1);
    get_status();
}


Text::Text(String input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy, uint16_t color)
    : MediaContainer(MediaType::TEXT, duration)
    , content(input)
    , font_id(ft_id)
    , cursor_x(cx)
    , cursor_y(cy) 
    , font_color(color) {
    set_status(MediaStatus::READY);
}

Text::Text(char* input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy, uint16_t color)
    : MediaContainer(MediaType::TEXT, duration)
    , content(String(input))
    , font_id(ft_id)
    , cursor_x(cx)
    , cursor_y(cy)
    , font_color(color) {
    set_status(MediaStatus::READY);
}

// APIs for Text
const uint8_t* Text::get_font() const {
    return map_font(font_id);
}
uint16_t Text::get_cursor_x() const {
    return cursor_x;
}
uint16_t Text::get_cursor_y() const {
    return cursor_y;
}
String Text::get_txt() const {
    return content;
}
uint16_t Text::get_font_color() const {
    return font_color;
}
FontID Text::get_font_id() const {
    return font_id;
}


TextGroup::TextGroup(size_t dur, uint16_t bg_col, uint16_t font_col, Rotation rot)
    : MediaContainer(MediaType::TEXTGROUP, dur)
    , next_idx(0)
    , bg_color(bg_col)
    , font_color(font_col)
    , rotation(rot) {
    set_status(MediaStatus::READY);
}

TextGroup::~TextGroup() {
    for (auto txt : vec) {
      delete txt;
    }
}

void TextGroup::add_member(MediaContainer* txt) {
    vec.push_back(txt);
}

size_t TextGroup::size() const {
    return vec.size();
}

MediaContainer* TextGroup::get_next() {
    if (vec.empty() || next_idx >= vec.size()) {
        return nullptr;
    }
    return vec[next_idx++];
}

uint16_t TextGroup::get_bg_color() const {
    return bg_color;
}

uint16_t TextGroup::get_font_color() const {
    return font_color;
}

Rotation TextGroup::get_rotation() const {
    return rotation;
}

void TextGroup::set_rotation(Rotation rot) {
    rotation = rot;
}


size_t Image::received_len() {
    return input_ptr - content;
}

int Image::JPEGDraw480(JPEGDRAW* pDraw) {
    // Serial.println("Drawing");
    Image* img = static_cast<Image*>(pDraw->pUser);
    
    // Safety check - ensure the image object is still valid
    if (img == nullptr || img->decoded_content == nullptr) {
        return 0; // Stop decoding if object is invalid
    }
    
    img->decode_mtx.lock();
    
    // For now, decode without rotation to isolate the crash issue
    // Rotation will be handled at render time instead
    
    for (int row = 0; row < pDraw->iHeight; row++) {
        // Calculate the source line within pPixels
        const uint16_t* src_line = ((uint16_t*)pDraw->pPixels) + row * pDraw->iWidth;

        for (int col = 0; col < pDraw->iWidth; col++) {
            uint16_t pixel = src_line[col];
            
            // Calculate destination coordinates (no rotation during decode)
            int dest_x = pDraw->x + col;
            int dest_y = pDraw->y + row;
            
            // Bounds check and write pixel
            if (dest_x >= 0 && dest_x < 480 && dest_y >= 0 && dest_y < 480) {
                img->decoded_content[dest_y * 480 + dest_x] = pixel;
            }
        }
    }
    
    img->decode_mtx.unlock();
    return 1;   // continue decode
}

int Image::JPEGDraw240(JPEGDRAW* pDraw) {
    Image* img = static_cast<Image*>(pDraw->pUser);
    
    // Safety check - ensure the image object is still valid
    if (img == nullptr || img->decoded_content == nullptr) {
        return 0; // Stop decoding if object is invalid
    }
    
    img->decode_mtx.lock();

    // For now, decode without rotation to isolate the crash issue
    // Rotation will be handled at render time instead

    for (int row = 0; row < pDraw->iHeight; row++) {
        // 1) Figure out the source pointer for this row in the chunk
        const uint16_t* src_line = 
            reinterpret_cast<const uint16_t*>(pDraw->pPixels) + row * pDraw->iWidth;

        for (int col = 0; col < pDraw->iWidth; col++) {
            uint16_t pixel = src_line[col];
            
            // Calculate source coordinates (before scaling)
            int src_x = pDraw->x + col;
            int src_y = pDraw->y + row;
            
            // Scale to 480x480 (each 240 pixel becomes 2x2 block)
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    int dest_x = src_x * 2 + dx;
                    int dest_y = src_y * 2 + dy;
                    
                    // Bounds check and write pixel (no rotation during decode)
                    if (dest_x >= 0 && dest_x < 480 && dest_y >= 0 && dest_y < 480) {
                        img->decoded_content[dest_y * 480 + dest_x] = pixel;
                    }
                }
            }
        }
    }

    img->decode_mtx.unlock();
    return 1;   // continue decode
}


void Image::decode() {
    // Check if we still have valid content
    if (content == nullptr || decoded_content == nullptr) {
        Serial.println("[IMAGE] ERROR: Invalid buffers for ID " + String(image_id));
        set_status(MediaStatus::EXPIRED);
        return;
    }
    
    unsigned long decode_start = millis();
    
    // Check memory for larger images only
    if (total_size > 50000 && psramFound()) {
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (free_psram < 65536) { // Less than 64KB free
            Serial.println("[IMAGE] WARNING: Low PSRAM (" + String(free_psram/1024) + "KB) for ID " + String(image_id));
        }
    }
    
    // Feed watchdog before starting intensive operation
    esp_task_wdt_reset();
    
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    
    // Set the drawing function based on resolution
    int (*JPEGDraw) (JPEGDRAW*) = (resolution == ImageResolution::SQ240) ? JPEGDraw240 : JPEGDraw480;
    
    // Decode
    if (jpeg.openRAM(content, total_size, JPEGDraw)) {
        jpeg.setUserPointer(this);
        esp_task_wdt_reset();
        
        if (jpeg.decode(0, 0, 0)) {
            set_status(MediaStatus::READY);
        } else {
            Serial.println("[IMAGE] ERROR: JPEG decode failed for ID " + String(image_id));
            set_status(MediaStatus::EXPIRED);
        }
        jpeg.close();
        esp_task_wdt_reset();
        
    } else {
        Serial.println("[IMAGE] ERROR: Failed to open JPEG for ID " + String(image_id) + 
                       " (size: " + String(total_size) + ")");
        set_status(MediaStatus::EXPIRED);
    }
    
    set_status(MediaStatus::READY);
}

void Image::startDecode() {
    set_status(MediaStatus::DECODING);
    xTaskCreatePinnedToCore(decodeTask, "DecodeTask", 8192, this, 1, &decodeTaskHandle, 0);
    // Serial.println("Task started");
}

Image::Image(uint8_t img_id, ImageFormat format, ImageResolution res, uint32_t total_img_size, size_t duration, uint8_t num_chunks, Rotation rot)
    : MediaContainer(MediaType::IMAGE, duration)
    , image_id(img_id)
    , image_format(format)
    , resolution(res)
    , total_size(total_img_size)
    , rotation(rot)
    , content(nullptr)
    , input_ptr(nullptr)
    , decoded_content(nullptr)
    , decodeTaskHandle(nullptr)
    , chunks_received(0)
    , expected_chunks(num_chunks)
    , chunk_received_mask(nullptr)
    , transfer_start_time(0)
    , chunk_timeout_ms(100 * num_chunks) {
    
    Serial.println("[IMAGE] Constructor: img_id " + String(img_id) + ", expected chunks: " + String(num_chunks));
    
    // Allocate chunk tracking mask (1 byte can track up to 8 chunks, extend as needed)
    size_t mask_size = (num_chunks + 7) / 8; // Round up to nearest byte
    chunk_received_mask = (uint8_t*) malloc(mask_size);
    if (chunk_received_mask) {
        memset(chunk_received_mask, 0, mask_size);
    } else {
        Serial.println("[IMAGE] ERROR: Failed to allocate chunk tracking mask");
        set_status(MediaStatus::EXPIRED);
        return;
    }
    
    // Check PSRAM availability for larger images
    if (total_img_size > 50000 && psramFound()) {
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t total_needed = total_img_size + (SCREEN_PXLCNT * sizeof(uint16_t));
        
        if (free_psram < total_needed + 32768) { // Keep 32KB buffer
            Serial.println("[IMAGE] WARNING: Low PSRAM for ID " + String(img_id) + 
                           " - Available: " + String(free_psram/1024) + "KB, Need: " + 
                           String(total_needed/1024) + "KB");
        }
    }
    
    // Allocate content buffer in PSRAM
    if (psramFound()) {
        content = (uint8_t*) ps_malloc(total_img_size);
    } else {
        content = (uint8_t*) malloc(total_img_size);
    }
    
    if (content == nullptr) {
        Serial.println("[IMAGE] ERROR: Failed to allocate content buffer (" + String(total_img_size) + " bytes)");
        if (psramFound()) {
            Serial.println("[IMAGE] PSRAM free: " + String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        }
        Serial.println("[IMAGE] Regular heap free: " + String(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
        set_status(MediaStatus::EXPIRED);
        return;
    }
    
    // Allocate decoded content buffer in PSRAM
    size_t decoded_size = SCREEN_PXLCNT * sizeof(uint16_t);
    if (psramFound()) {
        decoded_content = (uint16_t*) ps_malloc(decoded_size);
    } else {
        decoded_content = (uint16_t*) malloc(decoded_size);
    }
    
    if (decoded_content == nullptr) {
        Serial.println("[IMAGE] ERROR: Failed to allocate decoded buffer (" + String(decoded_size) + " bytes)");
        if (psramFound()) {
            Serial.println("[IMAGE] PSRAM free: " + String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        }
        Serial.println("[IMAGE] Regular heap free: " + String(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
        free(content);
        content = nullptr;
        set_status(MediaStatus::EXPIRED);
        return;
    }
    
    // Clear the decoded buffer
    memset(decoded_content, 0, decoded_size);
    
    input_ptr = content;
}

Image::~Image() {
    // Stop decoding task if still running
    if (decodeTaskHandle != nullptr) {
        vTaskDelete(decodeTaskHandle);
        decodeTaskHandle = nullptr;
    }
    
    // Free PSRAM buffers
    if (content != nullptr) {
        free(content);
        content = nullptr;
        input_ptr = nullptr;
    }
    
    if (decoded_content != nullptr) {
        free(decoded_content);
        decoded_content = nullptr;
    }
    
    // Free chunk tracking mask
    if (chunk_received_mask != nullptr) {
        free(chunk_received_mask);
        chunk_received_mask = nullptr;
    }
}

uint16_t* Image::get_img() {
    if (get_status() < MediaStatus::READY) {
        return nullptr;
    }
    return decoded_content;
}

void Image::add_chunk(const uint8_t* chunk, size_t chunk_size) {
    if (!content || !input_ptr) {
        Serial.println("[IMAGE] ERROR: Cannot add chunk - buffers not allocated for ID " + String(image_id));
        return;
    }
    
    if (input_ptr + chunk_size > content + total_size) {
        Serial.println("[IMAGE] ERROR: Chunk overflow for ID " + String(image_id) + 
                       " - Current: " + String(received_len()) + 
                       ", Adding: " + String(chunk_size) + 
                       ", Total: " + String(total_size));
        set_status(MediaStatus::EXPIRED);
        return;
    }
    
    // Copy chunk data
    memcpy(input_ptr, chunk, chunk_size);
    input_ptr += chunk_size;
    chunks_received++;
    
    size_t received = received_len();
    
    // Debug: Track chunk reception for each image
    Serial.println("[CHUNK] Image " + String(image_id) + ": chunk " + String(chunks_received) + 
                   " (" + String(chunk_size) + " bytes) - Total: " + String(received) + 
                   "/" + String(total_size) + " (" + String((received * 100) / total_size) + "%)");

    if (received == total_size) {
        Serial.println("[IMAGE] Image " + String(image_id) + " complete: " + String(chunks_received) + 
                       " chunks, " + String(total_size) + " bytes total");
        
        if (image_format == ImageFormat::JPEG) {
            startDecode();
        } else if (image_format == ImageFormat::RGB565) {
            // For RGB565 bitmap, no decoding needed
            size_t copy_size = min(total_size, SCREEN_PXLCNT * sizeof(uint16_t));
            memcpy(decoded_content, content, copy_size);
            set_status(MediaStatus::READY);
        } else {
            Serial.println("[IMAGE] ERROR: Unsupported format " + 
                           String(static_cast<uint8_t>(image_format)) + " for ID " + String(image_id));
            set_status(MediaStatus::EXPIRED);
        }
    }
}

// Override get_status to check for transfer timeout
MediaStatus Image::get_status() {
    // Check timeout first
    if (check_transfer_timeout()) {
        return MediaStatus::EXPIRED;
    }
    
    // Call parent implementation for regular status checks
    return MediaContainer::get_status();
}

bool Image::check_transfer_timeout() {
    if (transfer_start_time == 0) {
        return false; // No transfer started yet
    }
    
    if (all_chunks_received()) {
        return false; // All chunks received, no timeout
    }
    
    unsigned long elapsed = millis() - transfer_start_time;
    if (elapsed >= chunk_timeout_ms) {
        Serial.println("[IMAGE] Transfer timeout for ID " + String(image_id) + 
                       " - Expected: " + String(expected_chunks) + 
                       " chunks, Received: " + String(chunks_received) + 
                       " chunks after " + String(elapsed) + "ms");
        set_status(MediaStatus::EXPIRED);
        return true;
    }
    
    return false;
}

void Image::mark_chunk_received(uint8_t chunk_id) {
    if (!chunk_received_mask || chunk_id >= expected_chunks) {
        return;
    }
    
    // Set bit for this chunk
    size_t byte_idx = chunk_id / 8;
    uint8_t bit_idx = chunk_id % 8;
    chunk_received_mask[byte_idx] |= (1 << bit_idx);
    
    // Start timeout tracking on first chunk
    if (transfer_start_time == 0) {
        transfer_start_time = millis();
    }
}

bool Image::all_chunks_received() const {
    if (!chunk_received_mask) {
        return false;
    }
    
    // Check all expected chunks are received
    for (uint8_t chunk_id = 0; chunk_id < expected_chunks; chunk_id++) {
        size_t byte_idx = chunk_id / 8;
        uint8_t bit_idx = chunk_id % 8;
        if (!(chunk_received_mask[byte_idx] & (1 << bit_idx))) {
            return false; // This chunk not received yet
        }
    }
    return true;
}

void Image::add_chunk_with_id(const uint8_t* chunk, size_t chunk_size, uint8_t chunk_id) {
    if (!content || !input_ptr) {
        Serial.println("[IMAGE] ERROR: Cannot add chunk - buffers not allocated for ID " + String(image_id));
        return;
    }
    
    // Mark this chunk as received
    mark_chunk_received(chunk_id);
    
    if (input_ptr + chunk_size > content + total_size) {
        Serial.println("[IMAGE] ERROR: Chunk overflow for ID " + String(image_id) + 
                       " - Current: " + String(received_len()) + 
                       ", Adding: " + String(chunk_size) + 
                       ", Total: " + String(total_size));
        set_status(MediaStatus::EXPIRED);
        return;
    }
    
    // Copy chunk data
    memcpy(input_ptr, chunk, chunk_size);
    input_ptr += chunk_size;
    chunks_received++;
    
    size_t received = received_len();
    
    // Debug: Track chunk reception for each image
    Serial.println("[CHUNK] Image " + String(image_id) + ": chunk " + String(chunk_id) + 
                   " (" + String(chunk_size) + " bytes) - Total: " + String(received) + 
                   "/" + String(total_size) + " (" + String((received * 100) / total_size) + "%)");

    // Check if all chunks received or if we have complete data
    if (all_chunks_received() || received == total_size) {
        Serial.println("[IMAGE] Image " + String(image_id) + " complete: " + String(chunks_received) + 
                       " chunks, " + String(total_size) + " bytes total");
        
        if (image_format == ImageFormat::JPEG) {
            startDecode();
        } else if (image_format == ImageFormat::RGB565) {
            // For RGB565 bitmap, no decoding needed
            size_t copy_size = min(total_size, SCREEN_PXLCNT * sizeof(uint16_t));
            memcpy(decoded_content, content, copy_size);
            set_status(MediaStatus::READY);
        } else {
            Serial.println("[IMAGE] ERROR: Unsupported format " + 
                           String(static_cast<uint8_t>(image_format)) + " for ID " + String(image_id));
            set_status(MediaStatus::EXPIRED);
        }
    }
}

void Image::add_decoded(const uint16_t* img) {
  memcpy(decoded_content, img, SCREEN_PXLCNT * sizeof(uint16_t));
  set_status(MediaStatus::READY);
}


MediaContainer* print_error(String input) {
    TextGroup* group = new TextGroup(0, DICE_DARKGREY, DICE_WHITE);
    group->add_member(new Text("DEBUG Info:", 0, FontID::TF, 40, 40));
    group->add_member(new Text(input, 0, FontID::TF, 40, 160));
    Serial.println("[ERROR]: " + input);
    return group;
}

}   // namespace dice
