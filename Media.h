#ifndef DICE_MEDIA
#define DICE_MEDIA

#include <mutex>
#include <string>
#include <vector>

#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>
#include <U8g2lib.h>

#include "oldScreen_Driver.h"


namespace dice {

// Enums for Media Status
enum class MediaStatus : uint8_t { NOT_RECEIVED = 0, DECODING = 2, READY = 3, DISPLAYING = 4, EXPIRED = 5 };

// Enums for Media Types
enum class MediaType : uint8_t { TEXT = 0, TEXTGROUP = 1, IMAGE = 2, OPTION = 3, GIF = 5, CTLR = 255 };

// Enums for Image Formats
enum class ImageFormat : uint8_t { NOIMG = 0, JPEG480 = 1, JPEG240 = 2, BMP480 = 3, BMP240 = 4 };

// Enums for Font IDs
enum class FontID : uint8_t { NOTEXT = 0, TF = 1, ARABIC = 2, CHINESE = 3, CYRILLIC = 4, DEVANAGARI = 5 };

enum class PrettyColor : uint32_t { DARKGREY = 0x636363, BABYBLUE = 0xbee3f5, BLACK = 0x000000, WHITE = 0xffffff };

enum class Command : uint8_t {BACKLIGHT_OFF =1, BACKLIGHT_ON=2, OPTION_ID=3, }
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
    ~MediaContainer();

    MediaType get_media_type() const;

    // APIs for Options
    virtual MediaStatus get_status();

    virtual void trigger_display();

    // APIs for Text
    virtual const uint8_t* get_font() const = 0;
    virtual uint16_t get_cursor_x() const = 0;
    virtual uint16_t get_cursor_y() const = 0;
    virtual String get_txt() const = 0;

    // APIs for Image
    virtual void add_chunk(uint8_t* chunk, size_t chunk_size) = 0;
    virtual void add_decoded(const uint16_t* img) = 0;
    virtual uint16_t* get_img() = 0;

    // APIs for TextGroup
    virtual void add_member(MediaContainer* txt) = 0;
    virtual MediaContainer* get_next() = 0;
    virtual size_t size() const = 0;
    virtual uint16_t get_bg_color() const = 0;
    virtual uint16_t get_font_color() const = 0;

    // APIs for OptionGroup
    virtual String get_option_text(uint8_t id) const = 0;
    virtual uint8_t get_selected_index() const = 0;
    virtual void set_selected_index(uint8_t idx) = 0;
    virtual void add_option(String option_text) = 0;

    // // APIs for Controls
    // virtual void parse(uint8_t* payload, size_t payload_len);      //
    // virtual void apply();           // Take command into effect
};


class Image : public MediaContainer {
private:
    uint8_t* content;
    const size_t content_len;
    uint8_t* input_ptr;
    const uint8_t resolution;

    // Decoding parameters
    uint16_t* decoded_content;
    uint16_t* decode_input_ptr;
    TaskHandle_t decodeTaskHandle;

    JPEGDEC jpeg;
    std::mutex decode_mtx;   // Mutex for thread-safe access

    size_t received_len();

    // JPEGDraw callback function to handle drawing decoded JPEG blocks
    static int JPEGDraw(JPEGDRAW* pDraw);
    static void decodeTask(void* pvParameters);
    void decode();
    void upscale_2x();
    void startDecode();

public:
    Image(uint8_t img_id, ImageFormat format, uint32_t total_img_size, size_t duration);
    ~Image();

    virtual uint16_t* get_img();
    virtual void add_chunk(uint8_t* chunk, size_t chunk_size);

    virtual void add_decoded(const uint16_t* img);
};


class Text : public MediaContainer {
private:
    String content;
    FontID font;
    const uint16_t cursor_x, cursor_y;

public:
    Text(String input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy);
    Text(char* input, size_t duration, FontID ft_id, const uint16_t cx, uint16_t cy);

    // APIs for Text
    virtual const uint8_t* get_font() const;
    virtual uint16_t get_cursor_x() const;
    virtual uint16_t get_cursor_y() const;
    virtual String get_txt() const;

    // Map font IDs to font pointers
    static const uint8_t* map_font(FontID font_id);
};


class TextGroup : public MediaContainer {
private:
    std::vector<MediaContainer*> vec;
    size_t next_idx;
    const uint8_t color;

public:
    TextGroup(const size_t dur, const uint8_t col);
    ~TextGroup();

    virtual void add_member(MediaContainer* txt);
    virtual size_t size() const;

    virtual MediaContainer* get_next();
    virtual uint8_t get_bg_color() const;
    virtual uint8_t get_font_color() const;
};


class OptionGroup : public MediaContainer {
private:
    std::vector<MediaContainer*> vec;
    const uint8_t selecting;

public:
    OptionGroup(const uint8_t sel);

    virtual void add_option(String option_text);
    virtual size_t size() const;
    virtual vector<String> get_option_text(uint8_t id) const;
    virtual uint8_t get_selected_index() const;
    virtual void set_selected_index(uint8_t idx);
};

// class Control : public MediaContainer {
// private:
//     const uint8_t field;

// public:
//     OptionUpdate(const uint8_t new_id);
//     virtual uint8_t get_selected_index() const;
// };

}   // namespace dice

#endif
