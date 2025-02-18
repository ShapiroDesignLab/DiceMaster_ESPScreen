#ifndef DICE_SCREEN
#define DICE_SCREEN

#include <deque>
#include <vector>

#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#include "Media.h"
// #include "imageh/rgb565_umlogo.h"
#include "imageh/umlogo_jpg.h"


namespace dice {

class Screen {
private:
    Arduino_XCA9554SWSPI* expander;
    Arduino_ESP32RGBPanel* rgbpanel;
    Arduino_RGB_Display* gfx;

    std::deque<MediaContainer*> display_queue;
    uint16_t* screen_buffer;
    MediaContainer* current_disp;

    bool is_next_ready();
    bool is_option_media(MediaContainer* med);

    // Draw image
    void draw_img(MediaContainer* med);
    void draw_bmp565(uint16_t* img);
    void draw_color(uint16_t color);
    void draw_textgroup(MediaContainer* tg);
    void draw_text(MediaContainer* txt);
    void display_next();

public:
    Screen();

    void enqueue(MediaContainer* med);

    void update();

    // Utility functions
    void set_backlight(bool to_on);
    bool down_button_pressed();
    bool up_button_pressed();
    void draw_startup_logo();
};

// Demo Functions
MediaContainer* get_demo_textgroup();

}   // namespace dice

#endif
