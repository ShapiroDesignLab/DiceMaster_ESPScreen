// SPDX-FileCopyrightText: 2023 Limor Fried for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <U8g2lib.h>

#include <Arduino_GFX_Library.h>

#include <Adafruit_FT6206.h>
#include <Adafruit_CST8XX.h>

#include "rgb565_umlogo_sq480.h"

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
//    ,1, 30000000
    );

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
// 4.0" 480x480 rectangle bar display
   480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
   expander, GFX_NOT_DEFINED /* RST */, tl040wvs03_init_operations, sizeof(tl040wvs03_init_operations));

bool touchOK = false;        // we will check if the touchscreen exists
bool isFocalTouch = false;

uint16_t * logo;
uint16_t * allWhite;


// Testing flag, DELETE AFTERWARDS
bool colored = true;

void setup(void)
{  
  Serial.begin(115200);
  //while (!Serial) delay(100);
  
#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif

  Serial.println("Beginning");
  // Init Display

  #ifdef GFX_EXTRA_PRE_INIT
    GFX_EXTRA_PRE_INIT();
  #endif


  Wire.setClock(1000000); // speed up I2C 
  if (!gfx->begin()) Serial.println("gfx->begin() failed!");
  

  Serial.println("Initialized!");

  // Check PSRAM Init Status
  if (psramInit()) Serial.println("\nPSRAM correctly initialized");
  else Serial.println("PSRAM not available");

  gfx->fillScreen(BLACK);
  gfx->setUTF8Print(true);

  Serial.println("Below is flag status: ");
  #ifdef U8G2_FONT_SUPPORT
  Serial.println("True");
  #endif

  expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
  expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

  logo = (uint16_t *) ps_malloc(gfx->width() * gfx->height() * sizeof(uint16_t));
  allWhite = (uint16_t *) ps_malloc(gfx->width() * gfx->height() * sizeof(uint16_t));
  if (logo) {
    fillLogo(logo, umlogo_sq480);
    fillColor(allWhite, 0xffff);
    gfx->draw16bitRGBBitmap(0, 0, logo, gfx->width(), gfx->height());
  }
}



void loop()
{
  unsigned long startTime = millis();
  if (colored) {
    gfx->draw16bitRGBBitmap(0, 0, logo, gfx->width(), gfx->height());
    colored = false;
  }
  else{
    // gfx->draw16bitRGBBitmap(0, 0, allWhite, gfx->width(), gfx->height());
    int16_t x1, y1;
    uint16_t w, h;
    gfx->setFont(u8g2_font_unifont_t_chinese2);
    gfx->setTextColor(RED);
    gfx->setCursor(1, 16);
    gfx->getTextBounds("历史", 1, 16, &x1, &y1, &w, &h);
    gfx->drawRect(x1 - 1, y1 - 1, w + 2, h + 2, RED);
    gfx->println("历史");
    colored = true;
  }

  // use the buttons to turn off
  if (! expander->digitalRead(PCA_BUTTON_DOWN)) {
    expander->digitalWrite(PCA_TFT_BACKLIGHT, LOW);
  }
  // and on the backlight
  if (! expander->digitalRead(PCA_BUTTON_UP)) {
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
  }

  Serial.println(millis() - startTime);
  delay(1000);
}


void fillLogo(uint16_t * scrBuffer, const uint16_t * source){
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

// // https://chat.openai.com/share/8edee522-7875-444f-9fea-ae93a8dfa4ec
// void generateColorWheel(uint16_t *colorWheel) {
//   int width = gfx->width();
//   int height = gfx->height();
//   int half_width = width / 2;
//   int half_height = height / 2;
//   float angle;
//   uint8_t r, g, b;
//   int index, scaled_index;

//   for(int y = 0; y < half_height; y++) {
//     for(int x = 0; x < half_width; x++) {
//       index = y * half_width + x;
//       angle = atan2(y - half_height / 2, x - half_width / 2);
//       r = uint8_t(127.5 * (cos(angle) + 1));
//       g = uint8_t(127.5 * (sin(angle) + 1));
//       b = uint8_t(255 - (r + g) / 2);
//       uint16_t color = RGB565(r, g, b);

//       // Scale this pixel into 4 pixels in the full buffer
//       for(int dy = 0; dy < 2; dy++) {
//         for(int dx = 0; dx < 2; dx++) {
//           scaled_index = (y * 2 + dy) * width + (x * 2 + dx);
//           colorWheel[scaled_index] = color;
//         }
//       }
//     }
//   }
// }
