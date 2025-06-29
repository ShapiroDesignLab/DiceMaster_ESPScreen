#ifndef DICE_MEDIA
#define DICE_MEDIA

#include <mutex>
#include <string>
#include <vector>

#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>

#include "constants.h"
using namespace DConstant;

namespace dice {

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

    // APIs for rotation support
    virtual Rotation get_rotation() const {return Rotation::ROT_0;}
    virtual void set_rotation(Rotation rot) {return;}

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
    Rotation rotation;

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
        // Clear the handle before deleting the task to prevent double-delete
        img->decodeTaskHandle = nullptr;
        vTaskDelete(nullptr);   // Delete task after completion
    }
    void decode();
    // void upscale_2x();
    // void upscale_2x_y();
    // Testing only
    // void mask_up(int32_t start, int32_t len);
    void startDecode();

public:
    Image(uint8_t img_id, ImageFormat format, ImageResolution res, uint32_t total_img_size, size_t duration, Rotation rot = Rotation::ROT_0);
    virtual ~Image();

    virtual uint16_t* get_img();
    virtual void add_chunk(const uint8_t* chunk, size_t chunk_size);
    virtual void add_decoded(const uint16_t* img);
    virtual uint8_t get_image_id() const;
    virtual ImageFormat get_image_format() const;
    virtual ImageResolution get_image_resolution() const;
    virtual Rotation get_rotation() const;
    virtual void set_rotation(Rotation rot);
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
            return u8g2_font_unifont_tf;  // Back to original with background
        case FontID::ARABIC:
            return u8g2_font_unifont_t_arabic;
        case FontID::CHINESE:
            return u8g2_font_unifont_t_chinese;
        case FontID::CYRILLIC:
            return u8g2_font_cu12_t_cyrillic;
        case FontID::DEVANAGARI:
            return u8g2_font_unifont_t_devanagari;
            // return u8g2_font_6x13_t_devangari;
        default:
            return u8g2_font_unifont_tf;  // Back to original with background
        }
    }
};


class TextGroup : public MediaContainer {
private:
    std::vector<MediaContainer*> vec;
    size_t next_idx;
    const uint16_t bg_color;
    const uint16_t font_color;
    Rotation rotation;

public:
    TextGroup(const size_t dur, const uint16_t bg_col, const uint16_t font_col, Rotation rot = Rotation::ROT_0);
    virtual ~TextGroup();

    virtual void add_member(MediaContainer* txt);
    virtual size_t size() const;

    virtual MediaContainer* get_next();
    virtual uint16_t get_bg_color() const;
    virtual uint16_t get_font_color() const;
    virtual Rotation get_rotation() const;
    virtual void set_rotation(Rotation rot);
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
