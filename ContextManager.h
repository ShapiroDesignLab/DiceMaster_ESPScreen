#ifndef DICEMASTER_CONTEXTMANAGER
#define DICEMASTER_CONTEXTMANAGER

#include <ArduinoSTL.h>
#include "Image.h"

#define STRATEGY_SEQUENTIAL 0
#define STRATEGY_RANDOM 1

class ContextManager{

  // Context manager manages image loading and buffering. 
private:
  vector<Media*> imgs;
  queue<uint16_t*> screen_buffer;
  queue<Media*> image_buffer;

  uint16_t media_ID = 1;
  const uint16_t n_buffer = 2;

public:

  ContextManager(){
    // Check PSRAM Init Status
    if (psramInit()) Serial.println("\nPSRAM correctly initialized");
    else Serial.println("PSRAM not available");

    for (uint16_t i = 0;i < n_buffer; i++) {
      uint16_t * new_buffer = ps_malloc(gfx->width() * gfx->height() * sizeof(uint16_t));
      ScreenBuffer.push(new_buffer);
    }
  };

  // Need to manage image files
  Media* newImage() {
    Media* = new Image()
  }

  Media* newVideo() {

  }

  
  // Need to have dict 
};


#endif