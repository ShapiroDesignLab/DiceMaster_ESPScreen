#ifndef DICE_SCREEN
#define DICE_SCREEN

#include <deque>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <Arduino_GFX_Library.h>
#include <U8g2lib.h>
#include "media.h"

namespace dice {

// Screen queue configuration
constexpr size_t SCREEN_MEDIA_QUEUE_SIZE = 32;  // Buffer up to 32 media items

class Screen {
private:
    Arduino_XCA9554SWSPI* expander;
    Arduino_ESP32RGBPanel* rgbpanel;
    Arduino_RGB_Display* gfx;

    // Single thread-safe queue for all media
    QueueHandle_t media_queue;
    SemaphoreHandle_t queue_mutex;  // Protect queue operations
    
    uint16_t* screen_buffer;
    MediaContainer* current_disp;

    int revolv_idx = 0;
    
    // Rotation caching for performance optimization
    Rotation current_gfx_rotation = Rotation::ROT_0;

    bool is_next_ready();
    
    // Draw image
    void draw_img(MediaContainer* med);
    void draw_bmp565(uint16_t* img);
    void draw_bmp565_rotated(uint16_t* img, Rotation rotation);
    void draw_color(uint16_t color);
    void draw_textgroup(MediaContainer* tg);
    void draw_text(MediaContainer* txt);
    void draw_text(MediaContainer* txt, Rotation rotation);
    void draw_text(MediaContainer* txt, Rotation rotation, uint16_t text_color);
    void display_next();
    
    // Rotation helper functions
    void transform_coordinates(uint16_t& x, uint16_t& y, Rotation rotation);
    void set_display_rotation(Rotation rotation);
    void set_gfx_rotation_cached(Rotation rotation);  // Cached rotation setter
    
    // Simplified queue processing - no need for internal transfer
    // void process_incoming_media();  // No longer needed

public:
    Screen();
    ~Screen();

    bool enqueue(MediaContainer* med);

    void update();

    // Utility functions
    void set_backlight(bool to_on);
    bool down_button_pressed();
    bool up_button_pressed();
    int num_queued();

    // Demo functions
    void draw_startup_logo();
};

// Demo Functions
MediaContainer* get_demo_textgroup();

}   // namespace dice

#endif
