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
    NOIMG = 0,
    JPEG480 = 1,
    JPEG240 = 2,
    BMP480 = 3,
    BMP240 = 4
};

// Enums for Font IDs
enum class FontID : uint8_t {
    NOTEXT = 0,
    TF = 1,
    ARABIC = 2,
    CHINESE = 3,
    CYRILLIC = 4,
    DEVANAGARI = 5
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

    void set_status(MediaStatus new_status);

public:
    MediaContainer(MediaType med_type, size_t dur);
    virtual ~MediaContainer();

    MediaType get_media_type() const;
    virtual MediaStatus get_status();
    virtual void trigger_display();

    // Virtual functions to be overridden by derived classes

    // APIs for Text
    virtual const uint8_t* get_font() const = 0;
    virtual uint16_t get_cursor_x() const = 0;
    virtual uint16_t get_cursor_y() const = 0;
    virtual String get_txt() const = 0;

    // APIs for TextGroup
    virtual void add_member(MediaContainer* txt) = 0;
    virtual MediaContainer* get_next() = 0;
    virtual size_t size() const = 0;
    virtual uint16_t get_bg_color() const = 0;
    virtual uint16_t get_font_color() const = 0;

    // APIs for Image
    virtual void add_chunk(uint16_t chunk_number, uint8_t* chunk, size_t chunk_size) = 0;
    virtual uint16_t* get_img() = 0;
    virtual uint8_t get_image_id() const = 0;
    virtual ImageFormat get_image_format() const = 0;

    // APIs for OptionGroup
    virtual String get_option_text(uint8_t id) const = 0;
    virtual uint8_t get_selected_index() const = 0;
    virtual void set_selected_index(uint8_t idx) = 0;
    virtual void add_option(String option_text) = 0;
};


// Map font IDs to font pointers
const uint8_t* map_font(FontID font_id) {
  switch (font_id) {
    case FontID::TF:
      return u8g2_font_unifont_tf;
    case FontID::ARABIC:
      return u8g2_font_unifont_t_arabic;
    case FontID::CHINESE:
      return u8g2_font_unifont_t_chinese;
    case FontID::CYRILLIC:
      return u8g2_font_cu12_t_cyrillic;
    case FontID::DEVANAGARI:
      return u8g2_font_unifont_t_devanagari;
    default:
      return u8g2_font_unifont_tf;
  }
}


class Text : public MediaContainer {
private:
    String content;
    FontID font_id;
    uint16_t cursor_x;
    uint16_t cursor_y;

public:
    Text(String input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy);
    // APIs for Text
    virtual const uint8_t* get_font() const;
    virtual uint16_t get_cursor_x() const;
    virtual uint16_t get_cursor_y() const;
    virtual String get_txt() const;
    virtual FontID get_font_id() const;
};

class TextGroup : public MediaContainer {
private:
    std::vector<MediaContainer*> vec;
    size_t next_idx;
    uint16_t bg_color;
    uint16_t font_color;

public:
    TextGroup(size_t dur, uint16_t bg_col, uint16_t font_col);
    virtual ~TextGroup();

    virtual void add_member(MediaContainer* txt);
    virtual size_t size() const;
    virtual MediaContainer* get_next();
    virtual uint16_t get_bg_color() const;
    virtual uint16_t get_font_color() const;
};

class Image : public MediaContainer {
private:
    uint8_t image_id;
    ImageFormat image_format;
    uint32_t total_size;

    uint8_t* content;
    size_t content_len;
    uint8_t* input_ptr;

    uint16_t* decoded_content;
    TaskHandle_t decodeTaskHandle;
    JPEGDEC jpeg;
    std::mutex decode_mtx;

    size_t received_len();
    static int JPEGDraw(JPEGDRAW *pDraw);
    static void decodeTask(void* pvParameters);
    void decode();
    void startDecode();

public:
    Image(uint8_t img_id, ImageFormat format, uint32_t total_img_size, size_t duration);
    virtual ~Image();

    virtual uint16_t* get_img();
    virtual void add_chunk(uint16_t chunk_number, uint8_t* chunk, size_t chunk_size);
    virtual uint8_t get_image_id() const;
    virtual ImageFormat get_image_format() const;
};



class OptionGroup : public MediaContainer {
private:
    std::vector<String> options;
    uint8_t selected_index;

public:
    OptionGroup(uint8_t selected_idx);
    virtual void add_option(String option_text);

    virtual size_t size() const;
    virtual String get_option_text(uint8_t id) const;
    virtual uint8_t get_selected_index() const;
    virtual void set_selected_index(uint8_t idx);
};

} // namespace dice

#endif
