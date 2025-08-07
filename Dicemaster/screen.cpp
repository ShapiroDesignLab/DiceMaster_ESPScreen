#include "screen.h"
#include "jpg.hs/logo.h"
#include "jpg.hs/revolving_umlogo.h"

namespace dice {

void printHeapStatus() {
    size_t freeHeap = ESP.getFreeHeap();
    String heapInfo = "Free Heap: " + String(ESP.getFreePsram()) + " bytes";
    Serial.println(heapInfo);
}

bool Screen::is_next_ready() {
    if (display_queue.empty()) {
        return false;
    }
    while ((!display_queue.empty()) && display_queue.front()->get_status() > MediaStatus::READY) {
        MediaContainer* m = display_queue.front();
        display_queue.pop_front();
        
        // Debug: Log when images are being deleted due to expiration
        if (m->get_media_type() == MediaType::IMAGE) {
            Serial.println("[SCREEN] Deleting expired Image ID " + String(m->get_image_id()) + 
                           " - Status: " + String((int)m->get_status()));
        }
        delete m;
    }
    if (display_queue.empty()) {
        return false;
    }
    return (display_queue.front()->get_status() == MediaStatus::READY);
}

// Draw image
void Screen::draw_img(MediaContainer* med) {
    if (med->get_media_type() != MediaType::IMAGE) {
        return;
    }
    uint16_t* img_arr = med->get_img();
    if (img_arr == nullptr) return;
    
    // Get rotation from the image
    Rotation rotation = med->get_rotation();
    draw_bmp565_rotated(img_arr, rotation);
}

void Screen::draw_bmp565(uint16_t* img) {
    gfx->draw16bitRGBBitmap(0, 0, img, gfx->width(), gfx->height());
}

void Screen::draw_bmp565_rotated(uint16_t* img, Rotation rotation) {
    if (rotation == Rotation::ROT_0) {
        // No rotation needed
        draw_bmp565(img);
        return;
    }
    
    int width = gfx->width();
    int height = gfx->height();
    
    // For rotation, we need to create a temporary buffer and copy rotated pixels
    // Use PSRAM for the temporary buffer
    uint16_t* rotated_buffer = (uint16_t*)ps_malloc(width * height * sizeof(uint16_t));
    if (!rotated_buffer) {
        Serial.println("[ERROR] Failed to allocate rotation buffer in PSRAM");
        // Fallback to non-rotated if memory allocation fails
        draw_bmp565(img);
        return;
    }
    
    Serial.println("[ROTATION] Applying rotation " + String(static_cast<uint8_t>(rotation) * 90) + " degrees");
    
    // Rotate pixel data based on rotation angle
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int src_x, src_y;
            
            switch (rotation) {
                case Rotation::ROT_90:
                    src_x = height - 1 - y;
                    src_y = x;
                    break;
                case Rotation::ROT_180:
                    src_x = width - 1 - x;
                    src_y = height - 1 - y;
                    break;
                case Rotation::ROT_270:
                    src_x = y;
                    src_y = width - 1 - x;
                    break;
                default:
                    src_x = x;
                    src_y = y;
                    break;
            }
            
            if (src_x >= 0 && src_x < width && src_y >= 0 && src_y < height) {
                rotated_buffer[y * width + x] = img[src_y * width + src_x];
            }
        }
    }
    
    gfx->draw16bitRGBBitmap(0, 0, rotated_buffer, width, height);
    free(rotated_buffer);
    Serial.println("[ROTATION] Rotation complete, buffer freed");
}

void Screen::draw_color(uint16_t color) {
    gfx->fillScreen(color);
}

// Helper function to transform coordinates based on rotation
void Screen::transform_coordinates(uint16_t& x, uint16_t& y, Rotation rotation) {
    int width = gfx->width();
    int height = gfx->height();
    uint16_t orig_x = x, orig_y = y;
    
    switch (rotation) {
        case Rotation::ROT_90:
            // For 90° rotation: transform point around center
            x = height - orig_y;
            y = orig_x;
            break;
        case Rotation::ROT_180:
            // For 180° rotation: transform point around center
            x = width - orig_x;
            y = height - orig_y;
            break;
        case Rotation::ROT_270:
            // For 270° rotation: transform point around center
            x = orig_y;
            y = width - orig_x;
            break;
        case Rotation::ROT_0:
        default:
            // No transformation needed
            break;
    }
}

// Set U8g2 display rotation
void Screen::set_display_rotation(Rotation rotation) {
    // Note: U8g2 rotation values are different from our enum
    // U8g2: U8G2_R0, U8G2_R1 (90°), U8G2_R2 (180°), U8G2_R3 (270°)
    uint8_t u8g2_rotation;
    switch (rotation) {
        case Rotation::ROT_90:
            u8g2_rotation = 1;  // U8G2_R1
            break;
        case Rotation::ROT_180:
            u8g2_rotation = 2;  // U8G2_R2
            break;
        case Rotation::ROT_270:
            u8g2_rotation = 3;  // U8G2_R3
            break;
        case Rotation::ROT_0:
        default:
            u8g2_rotation = 0;  // U8G2_R0
            break;
    }
    
    // Apply rotation to the display
    // Note: This would need to be implemented based on your specific display setup
    // For now, we'll handle rotation through coordinate transformation
}

// Optimized rotation setter that caches the current state
void Screen::set_gfx_rotation_cached(Rotation rotation) {
    // Only call setRotation if the rotation has actually changed
    if (current_gfx_rotation != rotation) {
        uint8_t gfx_rotation_value;
        switch (rotation) {
            case Rotation::ROT_90:
                gfx_rotation_value = 1;
                break;
            case Rotation::ROT_180:
                gfx_rotation_value = 2;
                break;
            case Rotation::ROT_270:
                gfx_rotation_value = 3;
                break;
            case Rotation::ROT_0:
            default:
                gfx_rotation_value = 0;
                break;
        }
        
        gfx->setRotation(gfx_rotation_value);
        current_gfx_rotation = rotation;
        
        // Debug logging for rotation changes
        Serial.println("[ROTATION] Changed GFX rotation to " + String(static_cast<uint8_t>(rotation) * 90) + "°");
    }
}

void Screen::draw_textgroup(MediaContainer* tg) {
    if (tg->get_media_type() != MediaType::TEXTGROUP){
        return;
    }
    // Use the TextGroup's background color, not hardcoded DICE_DARKGREY
    draw_color(tg->get_bg_color());
    gfx->setTextSize(2);
    // Set the TextGroup's font color
    uint16_t text_color = tg->get_font_color();
    gfx->setTextColor(text_color);

    // Get rotation for this text group
    Rotation rotation = tg->get_rotation();
    set_display_rotation(rotation);

    MediaContainer* next = tg->get_next();
    while (next != nullptr) {
        draw_text(next, rotation, text_color);  // Pass text color to draw_text
        next = tg->get_next();
    }
}

void Screen::draw_text(MediaContainer* txt, Rotation rotation, uint16_t text_color) {
    if (txt->get_media_type() != MediaType::TEXT){
        return;
    }
    
    // Get coordinates from the text object (always specified for 0° rotation)
    uint16_t x = txt->get_cursor_x();
    uint16_t y = txt->get_cursor_y();
    
    // Set font before any operations
    gfx->setFont(txt->get_font());
    
    // Use individual text color - it now always contains the correct color from the protocol
    uint16_t individual_color = txt->get_font_color();
    gfx->setTextColor(individual_color);
    
    // Apply display rotation using cached setter - Arduino GFX handles coordinate transformation automatically
    set_gfx_rotation_cached(rotation);
    
    // Use original coordinates - Arduino GFX will transform them based on rotation
    gfx->setCursor(x, y);
    
    String text = txt->get_txt();
    gfx->println(text);
    
    // Reset rotation for next operations (using cached setter)
    set_gfx_rotation_cached(Rotation::ROT_0);
}

// Overloaded version for backward compatibility
void Screen::draw_text(MediaContainer* txt, Rotation rotation) {
    // Use default color when not specified
    draw_text(txt, rotation, DICE_WHITE);
}

// Overloaded version for backward compatibility without rotation
void Screen::draw_text(MediaContainer* txt) {
    draw_text(txt, Rotation::ROT_0, DICE_WHITE);
}

void Screen::display_next() {
    if (display_queue.empty()) {
        return;
    }
    
    if (current_disp != nullptr) {
        delay(10); // Give any running tasks a moment
        delete current_disp;
    }

    current_disp = display_queue.front();
    display_queue.pop_front();

    switch (current_disp->get_media_type()) {
    case MediaType::IMAGE:
        draw_img(current_disp);
        break;
    case MediaType::TEXTGROUP:
        draw_textgroup(current_disp);
        break;
    case MediaType::TEXT:
        draw_text(current_disp);
        break;
    default:
        Serial.println("Unsupported Media Type Encountered!");
        break;
    }
    current_disp->trigger_display();
}

Screen::Screen()
    : expander(new Arduino_XCA9554SWSPI(PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI, &Wire, 0x3F))
    , rgbpanel(new Arduino_ESP32RGBPanel(
        TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK, TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5, TFT_G0, TFT_G1, TFT_G2, TFT_G3,
        TFT_G4, TFT_G5, TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5, 1 /* hsync_polarity */, 50 /* hsync_front_porch */,
        2 /* hsync_pulse_width */, 44 /* hsync_back_porch */, 1 /* vsync_polarity */, 16 /* vsync_front_porch */,
        2 /* vsync_pulse_width */, 18 /* vsync_back_porch */
        ))
    , gfx(new Arduino_RGB_Display(
        // 4.0" 480x480 rectangle bar display
        480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */, expander,
        GFX_NOT_DEFINED /* RST */, tl040wvs03_init_operations, sizeof(tl040wvs03_init_operations)))
    , screen_buffer((uint16_t*) ps_malloc(gfx->width() * gfx->height() * sizeof(uint16_t)))
    , current_disp(nullptr)
    , current_gfx_rotation(Rotation::ROT_0) {  // Initialize rotation cache
#ifdef GFX_EXTRA_PRE_INIT
    GFX_EXTRA_PRE_INIT();
#endif
#ifdef GFX_BL
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
#endif

    if (!gfx->begin()) Serial.println("gfx->begin() failed!");
    Serial.println("GFX Initialized!");

    Wire.setClock(1000000);   // speed up I2C

    gfx->fillScreen(DICE_BLACK);
    gfx->setUTF8Print(true);

    expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

    draw_startup_logo();

    Serial.println("Screen Initialized!");
}


void Screen::enqueue(MediaContainer* med) {
    // If nothing is added
    if (med == nullptr) return;

    // If adding image or text
    if (med->get_media_type() == MediaType::IMAGE || med->get_media_type() == MediaType::TEXTGROUP
        || med->get_media_type() == MediaType::TEXT) {
        display_queue.push_back(med);
        
        // Debug: Log when images are enqueued with their status and duration
        if (med->get_media_type() == MediaType::IMAGE) {
            Serial.println("[SCREEN] Enqueued Image ID " + String(med->get_image_id()) + 
                           " - Status: " + String((int)med->get_status()) + 
                           ", Queue size: " + String(display_queue.size()));
        }
        return;
    }
    return;
}

void Screen::update() {
    // If next is emergency like option, we dump enforced time rule
    // If next is ready and current image expires, we move on;
    if (!is_next_ready()) {   // If there is nothing to show, just return
        return;
    }
    if (current_disp == nullptr || current_disp->get_status() >= MediaStatus::EXPIRED) {
        display_next();
        return;
    }
    // Serial.println("Not printing due to previous not expiring");
}

// Utility functions
void Screen::set_backlight(bool to_on) {
    if (to_on == true) {
        expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
        return;
    }
    expander->digitalWrite(PCA_TFT_BACKLIGHT, LOW);
}

bool Screen::down_button_pressed() {
    return !expander->digitalRead(PCA_BUTTON_DOWN);
}

bool Screen::up_button_pressed() {
    return !expander->digitalRead(PCA_BUTTON_UP);
}

int Screen::num_queued() {
    return display_queue.size();
}

void Screen::draw_startup_logo() {
    try {
      MediaContainer* med = new Image(0, ImageFormat::JPEG, ImageResolution::SQ480, logo_SIZE, 500, 1, Rotation::ROT_0);
      int input_time = millis();
      med->add_chunk(logo, logo_SIZE);
      while (med->get_status() != MediaStatus::READY) {
        delay(5);
      }
      enqueue(med);
    }
    catch (...) {
      MediaContainer* err = print_error("Startup Logo Decoding Failed");
      enqueue(err);
    }
}

void Screen::draw_revolving_logo() {
    if (num_queued() > 1) {
        return;
    }
    const uint8_t* img_arr = revolving_umlogo_array[revolv_idx];
    int img_size = revolving_umlogo_sizes[revolv_idx];
    try {
      MediaContainer* med = new Image(0, ImageFormat::JPEG, ImageResolution::SQ240, img_size, 1, 1, Rotation::ROT_0);
      int input_time = millis();
      med->add_chunk(img_arr, img_size);
      while (med->get_status() != MediaStatus::READY) {
        delay(1);
      }
      enqueue(med);
      revolv_idx = (revolv_idx+1) % revolving_umlogo_num;
    }
    catch (...) {
      MediaContainer* err = print_error("Revolving Logo Decoding Failed");
      enqueue(err);
    }
}

}   // namespace dice
