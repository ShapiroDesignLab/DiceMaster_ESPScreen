#include "ESPScreen.h"
#include "FileManager.h"
#include "ContextManager.h"

#include "imageh/rgb565_umlogo.h"
#include "imageh/rgb565_author.h"
#include "imageh/rgb565_barman.h"
#include "imageh/rgb565_movie_star.h"
#include "imageh/rgb565_psychic.h"
#include "imageh/rgb565_queen.h"
#include "imageh/rgb565_sergeant.h"
#include "imageh/rgb565_text_1.h"
#include "imageh/rgb565_text_2.h"
#include "imageh/rgb565_text_3.h"
#include "imageh/rgb565_text_4.h"
#include "imageh/rgb565_text_5.h"
#include "imageh/rgb565_text_6.h"
#include "imageh/rgb565_text_7.h"
#include "imageh/rgb565_text_8.h"

// Testing flag, DELETE AFTERWARDS
bool colored = true;


uint16_t img_index = 0;
const uint16_t MAX_IMG_INDEX = 3;
const uint16_t * imgs[MAX_IMG_INDEX] = {umlogo,author,barman};
            // {umlogo,author,barman};
            // {movie_star,psychic,queen};
            // {sergeant,text_1,text_2};
            // text_3,text_4,text_5,
            // {text_6,text_7,text_8};

void setup(void)
{
  // Init serial
  Serial.begin(115200);
  Serial.println("Beginning");

  // FileManager
  SPIAgent* receiver = new SPIAgent();

  // // ContextManager
  // ContextManager* cm = new ContextManager();

  // Init Display
  ESPScreen* screen = new ESPScreen();

}


void loop()
{
  unsigned long startTime = millis();
  
  if (colored) {
    fillSrc(screen_buffer,imgs[img_index]);
    gfx->draw16bitRGBBitmap(0, 0, screen_buffer, gfx->width(), gfx->height());
    colored = false;
    img_index = (img_index + 1) % MAX_IMG_INDEX;
  }
  else{
    // gfx->draw16bitRGBBitmap(0, 0, allWhite, gfx->width(), gfx->height());
    // int16_t x1, y1;
    // uint16_t w, h;
    // gfx->setFont(u8g2_font_unifont_t_chinese2);
    // gfx->setTextColor(RED);
    // gfx->setCursor(1, 16);
    // gfx->getTextBounds("历史", 1, 16, &x1, &y1, &w, &h);
    // gfx->drawRect(x1 - 1, y1 - 1, w + 2, h + 2, RED);
    // gfx->println("历史");
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

  // Serial.println(millis() - startTime);
  delay(1000);
}

