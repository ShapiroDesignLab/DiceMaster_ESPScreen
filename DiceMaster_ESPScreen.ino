#include "SPI_Driver.h"
#include "Screen_Driver.h"

bool ESP_WORKING = true;
bool __DEBUG = true;

Screen* screen;
SPIDriver* spid;

void setup(void)
{
  // Init serial
  Serial.begin(115200);
  Serial.println("Beginning");

  // Check PSRAM Init Status
  if (psramInit()) Serial.println("\nPSRAM correctly initialized");
  else Serial.println("PSRAM not available");

  spid = new SPIDriver();
  screen = new Screen();
}

void loop(){
\
  // Draw image
  screen->draw_startup_logo();
  screen->update();
  delay(1000);

  // Draw Text
  screen->draw_text_demo();
  screen->update();
  delay(1000); 

  // spid->queue_cmd_msgs();
  // spid->process_msgs();
  // delay(1000);
}