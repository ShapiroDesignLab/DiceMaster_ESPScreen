#include "screen.h"

namespace dice {

bool Screen::is_next_ready() {
    if (display_queue.empty()) {
        return false;
    }
    while (!display_queue.empty() && display_queue.front()->get_status() > MediaStatus::READY) {
        MediaContainer* m = display_queue.front();
        display_queue.pop_front();
        delete m;
    }
    if (display_queue.empty()) {
        return false;
    }
    return (display_queue.front()->get_status() == MediaStatus::READY);
}

bool Screen::is_option_media(MediaContainer* med) {
    return (med->get_media_type() == MediaType::OPTION);
}


// Draw image
void Screen::draw_img(MediaContainer* med) {
    if (med->get_media_type() != MediaType::IMAGE) {
        return;
    }
    uint16_t* img_arr = med->get_img();
    if (img_arr == nullptr) return;
    draw_bmp565(img_arr);
}

void Screen::draw_bmp565(uint16_t* img) {
    gfx->draw16bitRGBBitmap(0, 0, img, gfx->width(), gfx->height());
}

void Screen::draw_color(uint16_t color) {
    gfx->fillScreen(color);
}

void Screen::draw_textgroup(MediaContainer* tg) {
    if (tg->get_media_type() != MediaType::TEXTGROUP){
        return;
    }
    draw_color(DARKGREY);
    gfx->setTextSize(2);

    MediaContainer* next = tg->get_next();
    while (next != nullptr) {
        draw_text(next);
        next = tg->get_next();
    }
}

void Screen::draw_text(MediaContainer* txt) {
    if (txt->get_media_type() != MediaType::TEXT){
        return;
    }
    gfx->setFont(txt->get_font());
    gfx->setCursor(txt->get_cursor_x(), txt->get_cursor_y());
    gfx->println(txt->get_txt());
}

void Screen::display_next() {
    if (display_queue.empty()) {
        return;
    }
    if (current_disp != nullptr) {
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
    , current_disp(nullptr) {
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

    gfx->fillScreen(BLACK);
    gfx->setUTF8Print(true);

    expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

    draw_startup_logo();

    // Serial.println("Screen Initialized!");
}


void Screen::enqueue(MediaContainer* med) {
    // If nothing is added
    if (med == nullptr) return;

    // If adding image or text
    if (med->get_media_type() == MediaType::IMAGE || med->get_media_type() == MediaType::TEXTGROUP
        || med->get_media_type() == MediaType::TEXT) {
        display_queue.push_back(med);
        return;
    }

    // If add options
    if (med->get_media_type() == MediaType::OPTION) {
        display_queue.push_front(med);
        if (!display_queue.empty()) return;
        display_queue.push_front(med);
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
    }
    Serial.println("Not printing due to previous not expiring");
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

void Screen::draw_startup_logo() {
    MediaContainer* med = new Image(0, ImageFormat::RGB565, ImageResolution::SQ480, 0, 0);
    med->add_decoded(umlogo);
    enqueue(med);
}


// Demo Functions
MediaContainer* get_demo_textgroup() {
    TextGroup* group = new TextGroup(0, DARKGREY, WHITE);
    group->add_member(new Text("Psíquico", 0, FontID::TF, 40, 40));
    group->add_member(new Text("Hellseher", 0, FontID::TF, 280, 40));
    group->add_member(new Text("экстрасенс", 0, FontID::CYRILLIC, 40, 160));
    group->add_member(new Text("Psychique", 0, FontID::TF, 280, 160));
    group->add_member(new Text("Psychic", 0, FontID::TF, 40, 280));
    group->add_member(new Text("मानसिक", 0, FontID::DEVANAGARI, 280, 280));
    group->add_member(new Text("靈媒", 0, FontID::CHINESE, 40, 400));
    group->add_member(new Text("نفسية", 0, FontID::ARABIC, 280, 400));
    return group;
}
}   // namespace dice
