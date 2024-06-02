#ifndef DICE_SCREEN
#define DICE_SCREEN

#include <deque>
#include <vector>

// #include <Adafruit_FT6206.h>   // These rae included in the demo file, but probably for 
// #include <Adafruit_CST8XX.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>

#include "imageh/rgb565_umlogo.h"
#include "Media.h"


extern bool __DEBUG;
extern bool ESP_WORKING;

// SETUP
static const bool BACKLIGHT_ON = true;
static const bool BACKLIGHT_OFF = false;

static const uint8_t WORK_DELAY = 1;
static const uint8_t HYB_DELAY = 200;



class Screen {
private:
  Arduino_XCA9554SWSPI* expander;
  Arduino_ESP32RGBPanel* rgbpanel;
  Arduino_RGB_Display* gfx;

  std::deque<MediaContainer*> display_queue;
  uint16_t * screen_buffer;
  MediaContainer* current_disp;

  bool is_next_ready() {
    if (display_queue.empty()) {return false;}
    while (!display_queue.empty() && display_queue.front()->get_status() > STATUS_READY) {
      MediaContainer* m = display_queue.front();
      display_queue.pop_front();
      delete m;
    }
    if (display_queue.empty()) {return false;}
    return (display_queue.front()->get_status() == STATUS_READY);
  }
  
  bool is_option_media(MediaContainer* med) {
    return (med->get_media_type() == MEDIA_OPTION_BLOCKING
    || med->get_media_type() == MEDIA_OPTION 
    || med->get_media_type() == MEDIA_OPTION_END);
  }

  void enqueue(MediaContainer* med) {
    // If nothing is added
    if (med == nullptr) return;

    // If adding image or text
    if (med->get_media_type() == MEDIA_IMAGE 
      || med->get_media_type() == MEDIA_TEXTGROUP
      || med->get_media_type() == MEDIA_TEXT) {
      display_queue.push_back(med);
      return;
    }

    // If add options
    if (med->get_media_type() == MEDIA_OPTION) {
      display_queue.push_front(med);
      if (!display_queue.empty() 
        && display_queue.front()->get_media_type() == MEDIA_OPTION_BLOCKING) 
        return;
      display_queue.push_front(new MediaContainer(MEDIA_OPTION_BLOCKING, 0)); 
      return;
    }

    // If end of options
    if (med->get_media_type() == MEDIA_OPTION_END) {
      while (!display_queue.empty() 
        && is_option_media(display_queue.front())) {
        display_queue.pop_front();
      }
      return;
    }

    // If adding options
    if (med->get_media_type() == MEDIA_BACKLIGHT_ON) {
      set_backlight(BACKLIGHT_ON);
      return;
    }
    if (med->get_media_type() == MEDIA_BACKLIGHT_OFF) {
      set_backlight(BACKLIGHT_OFF);
      return;
    }
    return;
  }

  // Draw image
  void draw_img(MediaContainer* med) {
    assert(med->get_media_type() == MEDIA_IMAGE);
    uint16_t* img_arr = med->get_img();
    if (img_arr == nullptr) {
      return;
    }
    draw_bitmap(img_arr);
  }

  void draw_bitmap(uint16_t* img) {
    gfx->draw16bitRGBBitmap(0, 0, img, gfx->width(), gfx->height());
  }

  void draw_color(uint16_t color){
    gfx->fillScreen(color);
  }

  void draw_textgroup(MediaContainer* tg) {
    if (__DEBUG) Serial.println(tg->get_media_type() == MEDIA_TEXTGROUP);
    assert(tg->get_media_type() == MEDIA_TEXTGROUP);
    draw_color(DARKGREY);
    gfx->setTextSize(2);

    MediaContainer* next = tg->get_next();
    while(next != nullptr) {
      draw_text(next);
      next = tg->get_next();
    }
  }

  void draw_text(MediaContainer* txt) {
    assert(txt->get_media_type() == MEDIA_TEXT);
    gfx->setFont(txt->get_font()); 
    gfx->setCursor(txt->get_cursor_x(), txt->get_cursor_y());
    gfx->println(txt->get_txt());
  }

  void display_next() {
    assert(!display_queue.empty());
    if (display_queue.empty()) {
      return;
    }
    if (current_disp != nullptr) {
      delete current_disp;
    }

    current_disp = display_queue.front();
    display_queue.pop_front();

    switch (current_disp->get_media_type()) {
      case MEDIA_IMAGE: 
        draw_img(current_disp);
        break;
      case MEDIA_TEXTGROUP:
        draw_textgroup(current_disp);
        break;
      case MEDIA_TEXT:
        draw_text(current_disp);
        break;
      default:
        Serial.println("Unsupported Media Type Encountered!");
        break;
    }
    current_disp->trigger();
  }

public:
  Screen()
  : expander(new Arduino_XCA9554SWSPI(PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI,&Wire, 0x3F))
  , rgbpanel(new Arduino_ESP32RGBPanel(
      TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
      TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
      TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
      TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,
      1 /* hsync_polarity */, 50 /* hsync_front_porch */, 2 /* hsync_pulse_width */, 44 /* hsync_back_porch */,
      1 /* vsync_polarity */, 16 /* vsync_front_porch */, 2 /* vsync_pulse_width */, 18 /* vsync_back_porch */
      ))
  , gfx(new Arduino_RGB_Display(
  // 4.0" 480x480 rectangle bar display
    480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED /* RST */, tl040wvs03_init_operations, sizeof(tl040wvs03_init_operations)))
  , screen_buffer((uint16_t*)ps_malloc(gfx->width() * gfx->height() * sizeof(uint16_t)))
  , current_disp(nullptr)
  {
    #ifdef GFX_EXTRA_PRE_INIT
      GFX_EXTRA_PRE_INIT();
    #endif
    #ifdef GFX_BL
      pinMode(GFX_BL, OUTPUT);
      digitalWrite(GFX_BL, HIGH);
    #endif

    if (!gfx->begin()) Serial.println("gfx->begin() failed!");
    Serial.println("GFX Initialized!");

    Wire.setClock(1000000); // speed up I2C 

    gfx->fillScreen(BLACK);
    gfx->setUTF8Print(true);

    expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

    draw_startup_logo();

    Serial.println("Screen Initialized!");
  }

  void enqueue_vec(std::vector<MediaContainer*> med) {
    Serial.println(med.size());
    for (size_t i = 0;i < med.size(); ++i) {
      enqueue(med[i]);
    }
    Serial.println(display_queue.size());
  }

  void update() {
    // If next is emergency like option, we dump enforced time rule
    // If next is ready and current image expires, we move on;
    if (!is_next_ready()) {      // If there is nothing to show, just return
      return;
    }
    if (current_disp == nullptr 
      || display_queue.front()->get_media_type() == MEDIA_OPTION 
      || current_disp->get_status() >= STATUS_EXPIRED) {
      display_next();
    }
    Serial.println("Not printing due to previous not expiring");
  }

  // Utility functions
  void set_backlight(bool status) {
    if (status == BACKLIGHT_ON){
      expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
      return;
    }
    expander->digitalWrite(PCA_TFT_BACKLIGHT, LOW);
  }

  bool down_button_pressed() {
    return !expander->digitalRead(PCA_BUTTON_DOWN);
  }

  bool up_button_pressed() {
    return !expander->digitalRead(PCA_BUTTON_UP);
  }

  // Demo Functions
  void draw_text_demo() {
    TextGroup* group = new TextGroup(0, RED);
    group->add_member(new Text("Psíquico", 0, u8g2_font_unifont_tf, 40, 40));
    group->add_member(new Text("Hellseher", 0, u8g2_font_unifont_tf, 280, 40));
    group->add_member(new Text("экстрасенс", 0, u8g2_font_cu12_t_cyrillic, 40, 160));
    group->add_member(new Text("Psychique", 0, u8g2_font_unifont_tf, 280, 160));
    group->add_member(new Text("Psychic", 0, u8g2_font_unifont_tf, 40, 280));
    group->add_member(new Text("मानसिक", 0, u8g2_font_unifont_t_devanagari, 280, 280));
    group->add_member(new Text("靈媒", 0, u8g2_font_unifont_t_chinese, 40, 400));
    group->add_member(new Text("نفسية", 0, u8g2_font_unifont_t_arabic, 280, 400));
    enqueue(group);
  }

  void draw_startup_logo() {
    MediaContainer* med = new Image((size_t)1, IMG_480, (size_t)0);
    med->add_decoded(umlogo);
    enqueue(med);
  }
};


#endif