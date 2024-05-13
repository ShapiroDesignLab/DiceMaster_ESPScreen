#ifndef DICEMASTER_ESPSCREEN
#define DICEMASTER_ESPSCREEN

#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_FT6206.h>
#include <Adafruit_CST8XX.h>

#define MODE_IMG 0
#define MODE_ANIM 1
#define MODE_TXT 2

// SETUP
Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
    PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI,
    &Wire, 0x3F);
    
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
    TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
    TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
    TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,
    1 /* hsync_polarity */, 50 /* hsync_front_porch */, 2 /* hsync_pulse_width */, 44 /* hsync_back_porch */,
    1 /* vsync_polarity */, 16 /* vsync_front_porch */, 2 /* vsync_pulse_width */, 18 /* vsync_back_porch */
    );

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
// 4.0" 480x480 rectangle bar display
   480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
   expander, GFX_NOT_DEFINED /* RST */, tl040wvs03_init_operations, sizeof(tl040wvs03_init_operations));

class ESPScreen {
private:
  uint8_t mode = MODE_IMG;
public:
  ESPScreen() {
    #ifdef GFX_EXTRA_PRE_INIT
      GFX_EXTRA_PRE_INIT();
    #endif

    if (!gfx->begin()) Serial.println("gfx->begin() failed!");
    Serial.println("GFX Initialized!");

    Wire.setClock(1000000); // speed up I2C 

    gfx->fillScreen(BLACK);
    gfx->setUTF8Print(true);

    expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

    screen_buffer = (uint16_t *) ps_malloc(gfx->width() * gfx->height() * sizeof(uint16_t));
    fillSrc(screen_buffer, umlogo);

    drawImg(screen_buffer);
  }

  void drawImg(uint16_t * source) {
    gfx->draw16bitRGBBitmap(0, 0, screen_buffer, gfx->width(), gfx->height());
  }

  void draw_text() {
    if (mode != MODE_TEXT) init_for_text();
    Serial.println()
  }

  void init_for_text(){
    // gfx->draw16bitRGBBitmap(0, 0, allWhite, gfx->width(), gfx->height());
    mode = MODE_TEXT;
  }

  void fillSrc(uint16_t * scrBuffer, const uint16_t * source){
    int width = gfx->width();
    int height = gfx->height();
    
    for(int y = 0; y < height; ++y){
      for(int x = 0; x < width; ++x) {
        scrBuffer[y*width + x] = source[y * width + x];
      }
    }
  }


  void fillColor(uint16_t * scrBuffer, const uint16_t color){
    int width = gfx->width();
    int height = gfx->height();
    
    for(int y = 0; y < height; ++y){
      for(int x = 0; x < width; ++x) {
        scrBuffer[y*width + x] = color;
      }
    }
  }
}


#endif