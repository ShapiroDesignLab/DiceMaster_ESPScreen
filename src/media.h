#ifndef DICE_MEDIA
#define DICE_MEDIA

#include <mutex>
#include <string>
#include <vector>

#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>

namespace dice {

// Enums for Media Status
enum class MediaStatus : uint8_t { NOT_RECEIVED = 0, DECODING = 2, READY = 3, DISPLAYING = 4, EXPIRED = 5 };

// Enums for Media Types
enum class MediaType : uint8_t { TEXT = 0, TEXTGROUP = 1, IMAGE = 2, OPTION = 3, GIF = 5, CTLR = 255 };

// Enums for Image Formats
enum class ImageFormat : uint8_t { NOIMG = 0, JPEG=1, RGB565=2, RGB222=3};

enum class ImageResolution: uint8_t {SQ480=1, SQ240=2};

// Enums for Font IDs
enum class FontID : uint8_t { NOTEXT=0, TF = 1, ARABIC=2, CHINESE=3, CYRILLIC=4, DEVANAGARI=5};

// enum class PrettyColor : uint32_t { DARKGREY = 0x636363, BABYBLUE = 0xbee3f5, BLACK = 0x000000, WHITE = 0xffffff };

// enum class Command : uint8_t {BACKLIGHT_OFF =1, BACKLIGHT_ON=2, OPTION_ID=3, };

// Screen Buffer Size
constexpr size_t SCREEN_PXLCNT = 480 * 480;

class MediaContainer {
protected:
    const MediaType media_type;
    MediaStatus status;
    std::mutex status_mtx;
    size_t duration;
    size_t start_time;

    // Private API for setting status
    void set_status(MediaStatus new_status);

public:
    MediaContainer(const MediaType med_type, const size_t dur);
    virtual ~MediaContainer();

    MediaType get_media_type() const;

    // APIs for Options
    virtual MediaStatus get_status();

    virtual void trigger_display();

    // APIs for Text
    virtual const uint8_t* get_font() const {return 0;}
    virtual uint16_t get_cursor_x() const {return 0;}
    virtual uint16_t get_cursor_y() const {return 0;}
    virtual String get_txt() const {return String();}

    // APIs for Image
    virtual void add_chunk(const uint8_t* chunk, size_t chunk_size) {return;}
    virtual void add_decoded(const uint16_t* img) {return;}
    virtual uint16_t* get_img() {return nullptr;}
    virtual uint8_t get_image_id() {return 0;}
    virtual ImageResolution get_image_resolution() {return ImageResolution::SQ480;}

    // APIs for TextGroup
    virtual void add_member(MediaContainer* txt) {return;}
    virtual MediaContainer* get_next() {return nullptr;}
    virtual size_t size() const {return 0;}
    virtual uint16_t get_bg_color() const {return 0;}
    virtual uint16_t get_font_color() const {return 0;}
    virtual FontID get_font_id() const {return FontID::NOTEXT;}

    // APIs for OptionGroup
    // virtual std::vector<String> get_option_text(uint8_t id) const {return std::vector<String>();}
    // virtual uint8_t get_selected_index() const {return 0;}
    // virtual std::vector<String> get_option_text(uint8_t id) const {return std::vector<String>();}
    // virtual void set_selected_index(uint8_t idx) {return;}
    // virtual void add_option(String option_text) {return;}

    // // APIs for Controls
    // virtual void parse(uint8_t* payload, size_t payload_len);      //
    // virtual void apply();           // Take command into effect
};


class Image : public MediaContainer {
private:
    uint8_t image_id;
    ImageFormat image_format;
    uint8_t* content;
    const size_t total_size;
    uint8_t* input_ptr;
    ImageResolution resolution;

    // Decoding parameters
    uint16_t* decoded_content;
    uint16_t* decode_input_ptr;
    TaskHandle_t decodeTaskHandle;

    JPEGDEC jpeg;
    std::mutex decode_mtx;   // Mutex for thread-safe access

    size_t received_len();

    // JPEGDraw callback function to handle drawing decoded JPEG blocks
    static int JPEGDraw480(JPEGDRAW* pDraw);
    static int JPEGDraw240(JPEGDRAW* pDraw);
    static void decodeTask(void* pvParameters) {
        Image* img = static_cast<Image*>(pvParameters);
        img->decode();
        vTaskDelete(nullptr);   // Delete task after completion
    }
    void decode();
    void upscale_2x();
    void upscale_2x_y();
    // Testing only
    void mask_up(int32_t start, int32_t len);
    void startDecode();

public:
    Image(uint8_t img_id, ImageFormat format, ImageResolution res, uint32_t total_img_size, size_t duration);
    virtual ~Image();

    virtual uint16_t* get_img();
    virtual void add_chunk(const uint8_t* chunk, size_t chunk_size);
    virtual void add_decoded(const uint16_t* img);
    virtual uint8_t get_image_id() const;
    virtual ImageFormat get_image_format() const;
    virtual ImageResolution get_image_resolution() const;
};


class Text : public MediaContainer {
private:
    String content;
    FontID font_id;
    const uint16_t cursor_x, cursor_y;

public:
    Text(String input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy);
    Text(char* input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy);
    virtual ~Text() {}

    // APIs for Text
    virtual const uint8_t* get_font() const;
    virtual uint16_t get_cursor_x() const;
    virtual uint16_t get_cursor_y() const;
    virtual String get_txt() const;

    virtual FontID get_font_id() const;

    // Map font IDs to font pointers
    static const uint8_t* map_font(FontID font_id) {
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
};


class TextGroup : public MediaContainer {
private:
    std::vector<MediaContainer*> vec;
    size_t next_idx;
    const uint16_t bg_color;
    const uint16_t font_color;

public:
    TextGroup(const size_t dur, const uint16_t bg_col, const uint16_t font_col);
    virtual ~TextGroup();

    virtual void add_member(MediaContainer* txt);
    virtual size_t size() const;

    virtual MediaContainer* get_next();
    virtual uint16_t get_bg_color() const;
    virtual uint16_t get_font_color() const;
};


// class OptionGroup : public MediaContainer {
// private:
//     std::vector<String> options;
//     const uint8_t selected_index;

// public:
//     OptionGroup(const uint8_t sel);

//     virtual void add_option(String option_text);
//     virtual size_t size() const;
//     virtual std::vector<String> get_option_text(uint8_t id) const;
//     virtual uint8_t get_selected_index() const;
//     virtual void set_selected_index(uint8_t idx);
// };

// class Control : public MediaContainer {
// private:
//     const uint8_t field;

// public:
//     OptionUpdate(const uint8_t new_id);
//     virtual uint8_t get_selected_index() const;
// };

MediaContainer* get_demo_textgroup();
MediaContainer* print_error(String input);

}   // namespace dice

#endif
