#ifndef DICE_SCREEN
#define DICE_SCREEN

#include <deque>
#include <vector>
#include <string>

#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include "Media.h"

// Include LVGL library
#include <lvgl.h>

namespace dice {

// Screen configurations and constants
constexpr bool BACKLIGHT_ON = true;
constexpr bool BACKLIGHT_OFF = false;

// Define any necessary delays
constexpr uint8_t WORK_DELAY = 1;
constexpr uint8_t HYB_DELAY = 200;

// // Screen hardware interface pins (Replace with actual pin definitions)
// #define PCA_TFT_RESET   // Define actual pin
// #define PCA_TFT_CS      // Define actual pin
// #define PCA_TFT_SCK     // Define actual pin
// #define PCA_TFT_MOSI    // Define actual pin
// #define PCA_TFT_BACKLIGHT   // Define actual pin
// #define PCA_BUTTON_DOWN     // Define actual pin
// #define PCA_BUTTON_UP       // Define actual pin

// #define TFT_DE          // Define actual pin
// #define TFT_VSYNC       // Define actual pin
// #define TFT_HSYNC       // Define actual pin
// #define TFT_PCLK        // Define actual pin
// #define TFT_R1          // Define actual pin
// #define TFT_R2          // Define actual pin
// #define TFT_R3          // Define actual pin
// #define TFT_R4          // Define actual pin
// #define TFT_R5          // Define actual pin
// #define TFT_G0          // Define actual pin
// #define TFT_G1          // Define actual pin
// #define TFT_G2          // Define actual pin
// #define TFT_G3          // Define actual pin
// #define TFT_G4          // Define actual pin
// #define TFT_G5          // Define actual pin
// #define TFT_B1          // Define actual pin
// #define TFT_B2          // Define actual pin
// #define TFT_B3          // Define actual pin
// #define TFT_B4          // Define actual pin
// #define TFT_B5          // Define actual pin

// Initialize operations for the display (Replace with actual operations)
extern const uint8_t tl040wvs03_init_operations[];
#define sizeof_tl040wvs03_init_operations   // Define actual size

class Screen {
private:
    // Screen hardware interfaces
    Arduino_XCA9554SWSPI* expander;
    Arduino_ESP32RGBPanel* rgbpanel;
    Arduino_RGB_Display* gfx;

    // LVGL display buffer and driver
    lv_disp_buf_t lv_disp_buf;
    lv_color_t* lv_buf;
    lv_disp_drv_t lv_disp_drv;

    // Media queue and current display
    std::deque<MediaContainer*> display_queue;
    MediaContainer* current_disp;

    // Backlight control
    bool backlight_status;

    // LVGL objects for options
    lv_obj_t* option_cont; // Container for options
    lv_obj_t* option_labels[3]; // Labels for the three options

    // Private methods
    bool is_next_ready();
    void display_next();

    // Drawing methods
    void draw_image(MediaContainer* media);
    void draw_textgroup(MediaContainer* media);
    void draw_text(MediaContainer* media);
    void draw_optiongroup(MediaContainer* media);
    void handle_backlight_control(MediaContainer* media);

    // LVGL-related methods
    void init_lvgl();
    static void lvgl_flush_callback(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p);

public:
    Screen();
    ~Screen();

    void enqueue(MediaContainer* media);
    void update();

    // Utility functions
    void set_backlight(bool status);
    bool down_button_pressed();
    bool up_button_pressed();
};

// Implementation of Screen class methods

Screen::Screen()
    : expander(nullptr)
    , rgbpanel(nullptr)
    , gfx(nullptr)
    , current_disp(nullptr)
    , backlight_status(BACKLIGHT_ON)
    , option_cont(nullptr)
{
    // Initialize hardware interfaces
    expander = new Arduino_XCA9554SWSPI(PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI, &Wire, 0x3F);
    rgbpanel = new Arduino_ESP32RGBPanel(
        TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
        TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
        TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
        TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,
        1 /* hsync_polarity */, 50 /* hsync_front_porch */, 2 /* hsync_pulse_width */, 44 /* hsync_back_porch */,
        1 /* vsync_polarity */, 16 /* vsync_front_porch */, 2 /* vsync_pulse_width */, 18 /* vsync_back_porch */
    );

    gfx = new Arduino_RGB_Display(
        480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
        expander, GFX_NOT_DEFINED /* RST */, tl040wvs03_init_operations, sizeof_tl040wvs03_init_operations
    );

    if (!gfx->begin()) {
        Serial.println("gfx->begin() failed!");
    }

    Wire.setClock(1000000); // speed up I2C

    gfx->fillScreen(BLACK);
    gfx->setUTF8Print(true);

    expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

    // Initialize LVGL
    init_lvgl();

    // Initialize option container and labels
    option_cont = nullptr;
    for (int i = 0; i < 3; ++i) {
        option_labels[i] = nullptr;
    }
}

Screen::~Screen() {
    delete gfx;
    delete rgbpanel;
    delete expander;
    // Free LVGL buffer
    free(lv_buf);
}

void Screen::init_lvgl() {
    lv_init();

    // Allocate buffer for LVGL
    lv_buf = (lv_color_t*)malloc(sizeof(lv_color_t) * gfx->width() * 10); // Adjust buffer height as needed
    lv_disp_buf_init(&lv_disp_buf, lv_buf, NULL, gfx->width() * 10);

    lv_disp_drv_init(&lv_disp_drv);
    lv_disp_drv.flush_cb = lvgl_flush_callback;
    lv_disp_drv.buffer = &lv_disp_buf;
    lv_disp_drv.hor_res = gfx->width();
    lv_disp_drv.ver_res = gfx->height();
    lv_disp_drv.user_data = this; // Pass the Screen instance to the flush callback
    lv_disp_drv_register(&lv_disp_drv);
}

void Screen::lvgl_flush_callback(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    Screen* screen = static_cast<Screen*>(drv->user_data);
    if (screen) {
        // Copy the LVGL buffer to the display
        screen->gfx->startWrite();
        screen->gfx->writeAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
        screen->gfx->writePixels((uint16_t*)color_p, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));
        screen->gfx->endWrite();

        lv_disp_flush_ready(drv);
    }
}

bool Screen::is_next_ready() {
    while (!display_queue.empty()) {
        MediaContainer* media = display_queue.front();
        if (media->get_status() == MediaStatus::READY) {
            return true;
        } else if (media->get_status() == MediaStatus::EXPIRED) {
            // Remove expired media
            display_queue.pop_front();
            delete media;
        } else {
            break;
        }
    }
    return false;
}

void Screen::display_next() {
    if (current_disp != nullptr) {
        delete current_disp;
        current_disp = nullptr;
    }

    if (!display_queue.empty()) {
        current_disp = display_queue.front();
        display_queue.pop_front();

        // Clear the screen before displaying new content
        lv_obj_clean(lv_scr_act());

        switch (current_disp->get_media_type()) {
            case MediaType::IMAGE:
                draw_image(current_disp);
                break;
            case MediaType::TEXTGROUP:
                draw_textgroup(current_disp);
                break;
            case MediaType::OPTION:
                draw_optiongroup(current_disp);
                break;
            case MediaType::BACKLIGHT_CONTROL:
                handle_backlight_control(current_disp);
                break;
            default:
                Serial.println("Unsupported Media Type Encountered!");
                break;
        }

        current_disp->trigger_display();
    }
}

void Screen::draw_image(MediaContainer* media) {
    if (media->get_media_type() != MediaType::IMAGE) return;
    uint16_t* img_data = media->get_img();
    if (img_data) {
        gfx->draw16bitRGBBitmap(0, 0, img_data, gfx->width(), gfx->height());
    }
}

void Screen::draw_textgroup(MediaContainer* media) {
    if (media->get_media_type() != MediaType::TEXTGROUP) return;
    TextGroup* text_group = static_cast<TextGroup*>(media);

    // Use LVGL to create labels with animations
    lv_obj_clean(lv_scr_act()); // Clear the screen

    lv_obj_t* scr = lv_disp_get_scr_act(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_make((text_group->get_bg_color() >> 11) << 3, ((text_group->get_bg_color() >> 5) & 0x3F) << 2, (text_group->get_bg_color() & 0x1F) << 3), 0);

    MediaContainer* txt_media = nullptr;
    while ((txt_media = text_group->get_next()) != nullptr) {
        Text* txt = static_cast<Text*>(txt_media);
        lv_obj_t* label = lv_label_create(scr);
        lv_label_set_text(label, txt->get_txt().c_str());
        lv_obj_set_pos(label, txt->get_cursor_x(), txt->get_cursor_y());

        // Set font if necessary
        // For simplicity, using default font here

        // Set font color
        lv_obj_set_style_text_color(label, lv_color_make((text_group->get_font_color() >> 11) << 3, ((text_group->get_font_color() >> 5) & 0x3F) << 2, (text_group->get_font_color() & 0x1F) << 3), 0);

        // Add fade-in animation
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, label);
        lv_anim_set_values(&a, 0, 255);
        lv_anim_set_time(&a, 500); // Animation time in milliseconds
        lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
            lv_obj_set_style_opa((lv_obj_t*)var, v, 0);
        });
        lv_anim_start(&a);
    }

    // Update LVGL
    lv_task_handler();
}

void Screen::draw_optiongroup(MediaContainer* media) {
    if (media->get_media_type() != MediaType::OPTION) return;
    OptionGroup* option_group = static_cast<OptionGroup*>(media);

    // Create a container for the options if it doesn't exist
    if (!option_cont) {
        option_cont = lv_obj_create(lv_scr_act());
        lv_obj_set_size(option_cont, gfx->width(), gfx->height());
        lv_obj_set_style_bg_color(option_cont, lv_color_white(), 0);
    }

    uint8_t selected_index = option_group->get_selected_index();
    size_t num_options = option_group->size();

    // Calculate indices for options to display
    int option_indices[3];
    option_indices[1] = selected_index; // Middle option (selected)
    option_indices[0] = (selected_index > 0) ? selected_index - 1 : -1; // Option above
    option_indices[2] = (selected_index + 1 < num_options) ? selected_index + 1 : -1; // Option below

    // Positions for the three labels
    int label_positions[3] = {
        gfx->height() / 2 - 60, // Above
        gfx->height() / 2 - 20, // Middle
        gfx->height() / 2 + 20  // Below
    };

    // Create or update labels
    for (int i = 0; i < 3; ++i) {
        if (option_indices[i] != -1) {
            String option_text = option_group->get_option_text(option_indices[i]);

            if (!option_labels[i]) {
                // Create label
                option_labels[i] = lv_label_create(option_cont);
            }

            // Set label text
            lv_label_set_text(option_labels[i], option_text.c_str());

            // Set label style
            lv_obj_set_style_text_color(option_labels[i], lv_color_black(), 0);
            lv_obj_set_style_bg_color(option_labels[i], lv_color_white(), 0);
            lv_obj_set_style_border_width(option_labels[i], (i == 1) ? 2 : 0, 0);
            lv_obj_set_style_border_color(option_labels[i], lv_color_hex(0x555555), 0);

            // Set position
            lv_obj_set_pos(option_labels[i], (gfx->width() - lv_obj_get_width(option_labels[i])) / 2, label_positions[i]);

            // Set size (optional)
            lv_obj_set_width(option_labels[i], gfx->width() - 40); // Leave some padding
            lv_label_set_long_mode(option_labels[i], LV_LABEL_LONG_WRAP);
        } else {
            // No option at this index
            if (option_labels[i]) {
                lv_obj_del(option_labels[i]);
                option_labels[i] = nullptr;
            }
        }
    }

    // Scroll animation if the selected index has changed
    static uint8_t last_selected_index = 0xFF;
    if (last_selected_index != 0xFF && last_selected_index != selected_index) {
        // Determine scroll direction
        int delta = selected_index - last_selected_index;
        int animation_offset = (delta > 0) ? -40 : 40;

        // Animate labels
        for (int i = 0; i < 3; ++i) {
            if (option_labels[i]) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, option_labels[i]);
                lv_anim_set_values(&a, lv_obj_get_y(option_labels[i]), lv_obj_get_y(option_labels[i]) + animation_offset);
                lv_anim_set_time(&a, 200);
                lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
                    lv_obj_set_y((lv_obj_t*)var, v);
                });
                lv_anim_start(&a);
            }
        }
    }

    last_selected_index = selected_index;

    // Update LVGL
    lv_task_handler();
}

void Screen::handle_backlight_control(MediaContainer* media) {
    // Assuming MediaContainer has necessary info for backlight control
    set_backlight(backlight_status);
}

void Screen::enqueue(MediaContainer* media) {
    if (!media) return;

    // Handle special media types immediately
    if (media->get_media_type() == MediaType::BACKLIGHT_CONTROL) {
        handle_backlight_control(media);
        delete media;
        return;
    }

    // Enqueue other media types
    display_queue.push_back(media);
}

void Screen::update() {
    if (is_next_ready()) {
        if (!current_disp || current_disp->get_status() == MediaStatus::EXPIRED) {
            display_next();
        } else if (current_disp->get_media_type() == MediaType::OPTION) {
            // Option selection might have been updated
            draw_optiongroup(current_disp);
        }
    }

    // Update LVGL tasks
    lv_task_handler();
}

void Screen::set_backlight(bool status) {
    backlight_status = status;
    if (status == BACKLIGHT_ON) {
        expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
    } else {
        expander->digitalWrite(PCA_TFT_BACKLIGHT, LOW);
    }
}

bool Screen::down_button_pressed() {
    return !expander->digitalRead(PCA_BUTTON_DOWN);
}

bool Screen::up_button_pressed() {
    return !expander->digitalRead(PCA_BUTTON_UP);
}

} // namespace dice

#endif
