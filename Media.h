#ifndef DICE_MEDIA
#define DICE_MEDIA

#include <mutex>
#include <vector>
#include <string>

#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>


namespace dice {

// Enums for Media Status
enum class MediaStatus : uint8_t {
    NOT_RECEIVED = 0,
    DECODING = 2,
    READY = 3,
    DISPLAYING = 4,
    EXPIRED = 5
};

// Enums for Media Types
enum class MediaType : uint8_t {
    TEXT = 0,
    TEXTGROUP = 1,
    IMAGE = 2,
    OPTION = 3,
    GIF = 5,
    BACKLIGHT_CONTROL = 6
};

// Enums for Image Formats
enum class ImageFormat : uint8_t {
    JPEG = 1,
    RGB565 = 2
};

// Enums for Font IDs
enum class FontID : uint8_t {
    DEFAULT = 0,
    ARIAL = 1,
    TIMES_NEW_ROMAN = 2
    // Add other font IDs as needed
};

// Screen Buffer Size
constexpr size_t SCREEN_BUF_SIZE = 480 * 480;

class MediaContainer {
protected:
    const MediaType media_type;
    MediaStatus status;
    std::mutex status_mtx;
    size_t duration;
    size_t start_time;

    void set_status(MediaStatus new_status) {
        std::lock_guard<std::mutex> lock(status_mtx);
        status = new_status;
    }

public:
    MediaContainer(MediaType med_type, size_t dur)
        : media_type(med_type)
        , status(MediaStatus::NOT_RECEIVED)
        , duration(dur)
        , start_time(0)
    {
        // Limit duration if necessary
    }

    virtual ~MediaContainer() {}

    MediaType get_media_type() const {
        return media_type;
    }

    virtual MediaStatus get_status() {
        bool expired = (start_time > 0 && (millis() - start_time) >= duration);
        if (expired) {
            set_status(MediaStatus::EXPIRED);
        }
        std::lock_guard<std::mutex> lock(status_mtx);
        return status;
    }

    virtual void trigger_display() {
        if (start_time != 0) return;   // Only trigger once
        set_status(MediaStatus::DISPLAYING);
        start_time = max(millis(), (long unsigned int)1);
        get_status();
    }

    // Virtual functions to be overridden by derived classes

    // APIs for Text
    virtual const uint8_t* get_font() const { return nullptr; }
    virtual uint16_t get_cursor_x() const { return 0; }
    virtual uint16_t get_cursor_y() const { return 0; }
    virtual String get_txt() const { return String(); }

    // APIs for TextGroup
    virtual void add_member(MediaContainer* txt) {}
    virtual MediaContainer* get_next() { return nullptr; }
    virtual size_t size() const { return 0; }
    virtual uint16_t get_bg_color() const { return 0; }
    virtual uint16_t get_font_color() const { return 0; }

    // APIs for Image
    virtual void add_chunk(uint16_t chunk_number, uint8_t* chunk, size_t chunk_size) {}
    virtual uint16_t* get_img() { return nullptr; }
    virtual uint8_t get_image_id() const { return 0; }
    virtual ImageFormat get_image_format() const { return ImageFormat::JPEG; }
    virtual uint16_t get_delay_time() const { return 0; }

    // APIs for OptionGroup
    virtual String get_option_text(uint8_t id) const { return String(); }
    virtual uint8_t get_selected_index() const { return 0; }
    virtual void set_selected_index(uint8_t idx) {}
    virtual void add_option(String option_text) {}
};

class Text : public MediaContainer {
private:
    String content;
    FontID font_id;
    uint16_t cursor_x;
    uint16_t cursor_y;

public:
    Text(String input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy)
        : MediaContainer(MediaType::TEXT, duration)
        , content(input)
        , font_id(ft_id)
        , cursor_x(cx)
        , cursor_y(cy)
    {
        set_status(MediaStatus::READY);
    }

    // APIs for Text
    virtual const uint8_t* get_font() const {
        return map_font(font_id);
    }
    virtual uint16_t get_cursor_x() const {
        return cursor_x;
    }
    virtual uint16_t get_cursor_y() const {
        return cursor_y;
    }
    virtual String get_txt() const {
        return content;
    }
    virtual FontID get_font_id() const {
        return font_id;
    }
};

class TextGroup : public MediaContainer {
private:
    std::vector<MediaContainer*> vec;
    size_t next_idx;
    uint16_t bg_color;
    uint16_t font_color;

public:
    TextGroup(size_t dur, uint16_t bg_col, uint16_t font_col)
        : MediaContainer(MediaType::TEXTGROUP, dur)
        , next_idx(0)
        , bg_color(bg_col)
        , font_color(font_col)
    {
        set_status(MediaStatus::READY);
    }

    virtual ~TextGroup() {
        for (auto txt : vec) {
            delete txt;
        }
    }

    virtual void add_member(MediaContainer* txt) {
        vec.push_back(txt);
    }

    virtual size_t size() const {
        return vec.size();
    }

    virtual MediaContainer* get_next() {
        if (vec.empty() || next_idx >= vec.size()) {
            return nullptr;
        }
        return vec[next_idx++];
    }

    virtual uint16_t get_bg_color() const {
        return bg_color;
    }

    virtual uint16_t get_font_color() const {
        return font_color;
    }
};

class Image : public MediaContainer {
private:
    uint8_t image_id;
    ImageFormat image_format;
    uint32_t total_size;
    uint16_t delay_time;

    uint8_t* content;
    size_t content_len;
    uint8_t* input_ptr;

    uint16_t* decoded_content;
    TaskHandle_t decodeTaskHandle;
    JPEGDEC jpeg;
    std::mutex decode_mtx;

    size_t received_len() { return input_ptr - content; }

    static int JPEGDraw(JPEGDRAW *pDraw) {
        Image *img = static_cast<Image*>(pDraw->pUser);
        img->decode_mtx.lock();
        uint16_t* destination = img->decoded_content + (pDraw->y * 480 + pDraw->x);
        memcpy(destination, pDraw->pPixels, pDraw->iWidth * pDraw->iHeight * sizeof(uint16_t));
        img->decode_mtx.unlock();
        return 1; // continue decode
    }

    static void decodeTask(void* pvParameters) {
        Image* img = static_cast<Image*>(pvParameters);
        img->decode();
        vTaskDelete(nullptr); // Delete task after completion
    }

    void decode() {
        jpeg.setPixelType(RGB565_BIG_ENDIAN); // Adjust as necessary
        if (jpeg.openRAM(content, content_len, JPEGDraw)) {
            jpeg.setUserPointer(this);
            if (jpeg.decode(0, 0, 0)) { // Decode at full scale
                set_status(MediaStatus::READY);
            }
            jpeg.close();
            free(content); // Free original content as it's no longer needed
            content = nullptr;
        } else {
            set_status(MediaStatus::EXPIRED); // Handle error appropriately
        }
    }

    void startDecode() {
        set_status(MediaStatus::DECODING);
        xTaskCreatePinnedToCore(decodeTask, "DecodeTask", 8192, this, 1, &decodeTaskHandle, 0);
    }

public:
    Image(uint8_t img_id, ImageFormat format, uint32_t total_img_size, uint16_t delay, size_t duration)
        : MediaContainer(MediaType::IMAGE, duration)
        , image_id(img_id)
        , image_format(format)
        , total_size(total_img_size)
        , delay_time(delay)
        , content((uint8_t*)ps_malloc(total_img_size))
        , content_len(total_img_size)
        , input_ptr(content)
        , decoded_content((uint16_t*)ps_malloc(SCREEN_BUF_SIZE * sizeof(uint16_t)))
        , decodeTaskHandle(nullptr)
    {
        if (content == nullptr || decoded_content == nullptr) {
            // Handle memory allocation failure
            set_status(MediaStatus::EXPIRED);
        }
    }

    virtual ~Image() {
        free(content);
        free(decoded_content);
    }

    virtual uint16_t* get_img() {
        if (get_status() < MediaStatus::READY) {
            return nullptr;
        }
        return decoded_content;
    }

    virtual void add_chunk(uint16_t chunk_number, uint8_t* chunk, size_t chunk_size) {
        if (input_ptr + chunk_size > content + content_len) {
            // Handle overflow
            return;
        }
        memcpy(input_ptr, chunk, chunk_size);
        input_ptr += chunk_size;

        if (received_len() == content_len) {
            if (image_format == ImageFormat::JPEG) {
                startDecode();
            } else if (image_format == ImageFormat::RGB565) {
                // For RGB565 bitmap, no decoding needed
                memcpy(decoded_content, content, content_len);
                free(content);
                content = nullptr;
                set_status(MediaStatus::READY);
            }
        }
    }

    virtual uint8_t get_image_id() const {
        return image_id;
    }

    virtual ImageFormat get_image_format() const {
        return image_format;
    }

    virtual uint16_t get_delay_time() const {
        return delay_time;
    }
};

// Map font IDs to font pointers
const uint8_t* map_font(FontID font_id) {
    switch (font_id) {
        case FontID::DEFAULT:
            return u8g2_font_unifont_tf;
        case FontID::ARIAL:
            return u8g2_font_helvB14_tf; // Example font
        case FontID::TIMES_NEW_ROMAN:
            return u8g2_font_timB14_tf; // Example font
        // Add cases for other font IDs
        default:
            return u8g2_font_unifont_tf;
    }
}

class OptionGroup : public MediaContainer {
private:
    std::vector<String> options;
    uint8_t selected_index;

public:
    OptionGroup(uint8_t selected_idx)
        : MediaContainer(MediaType::OPTION, 0)
        , selected_index(selected_idx)
    {
        set_status(MediaStatus::READY);
    }

    virtual void add_option(String option_text) {
        options.push_back(option_text);
    }

    virtual size_t size() const {
        return options.size();
    }

    virtual String get_option_text(uint8_t id) const {
        if (id >= options.size()) return String();
        return options[id];
    }

    virtual uint8_t get_selected_index() const {
        return selected_index;
    }

    virtual void set_selected_index(uint8_t idx) {
        if (idx < options.size()) {
            selected_index = idx;
        }
    }
};

} // namespace dice

#endif
