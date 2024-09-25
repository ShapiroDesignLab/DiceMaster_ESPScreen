#ifndef DICE_SCREEN
#define DICE_SCREEN

#include <deque>
#include <vector>
#include <string>

#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include "Media.h"

namespace dice {

// Screen configurations and constants
constexpr bool BACKLIGHT_ON = true;
constexpr bool BACKLIGHT_OFF = false;

// Define any necessary delays
constexpr uint8_t WORK_DELAY = 1;
constexpr uint8_t HYB_DELAY = 200;


class Screen {
private:
    // Screen hardware interfaces
    Arduino_XCA9554SWSPI* expander;
    Arduino_ESP32RGBPanel* rgbpanel;
    Arduino_RGB_Display* gfx;

    // Media queue and current display
    std::deque<MediaContainer*> display_queue;
    MediaContainer* current_disp;

    // Backlight control
    bool backlight_status;

    // Private methods
    bool is_next_ready();
    void display_next();

    // Drawing methods
    void draw_image(MediaContainer* media);
    void draw_textgroup(MediaContainer* media);
    void draw_text(MediaContainer* media);
    void draw_optiongroup(MediaContainer* media);
    void handle_backlight_control(MediaContainer* media);

public:
    Screen();
    ~Screen();

    void enqueue(MediaContainer* media);
    void update();

    // Utility functions
    void set_backlight(bool status);
    bool down_button_pressed();
    bool up_button_pressed();

    void draw_startup_logo();
};

// Demo Functions
MediaContainer* get_demo_textgroup();

} // namespace dice

#endif
