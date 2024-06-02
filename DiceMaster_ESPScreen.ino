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

  // screen->draw_text_demo();
  // screen->update();
}

void loop()
{
  // Queue Message Receipt
  spid->queue_cmd_msgs();
  
  // Draw image
  // screen->draw_startup_logo();
  // screen->update();

  // Draw Text
  // screen->draw_text_demo();
  screen->update();
  // delay(1000); 

  // Messages
  screen->enqueue_vec(spid->process_msgs());
  Serial.println("Message Received and Processed");
  delay(1);
}