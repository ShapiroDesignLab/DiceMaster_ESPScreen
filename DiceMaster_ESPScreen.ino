#include "screen.h"
#include "spi.h"

using namespace dice;

Screen* screen;
SPIDriver* spid;

void setup(void)
{
  // // Init serial
  // Serial.begin(115200);
  // Serial.println("Beginning");

  // Check PSRAM Init Status
  if (psramInit()) Serial.println("\nPSRAM correctly initialized");
  else Serial.println("PSRAM not available");

  spid = new SPIDriver();
  screen = new Screen();
}

void loop()
{
  /*******************
  BEGIN DEMO ONLY
  *******************/
  // Draw image
  screen->draw_startup_logo();
  screen->update();
  delay(1000); 

  // Draw Text
  screen->enqueue(get_demo_textgroup());
  screen->update();
  delay(1000); 
  /*******************
  END DEMO ONLY
  *******************/

  // Queue Message Receipt
  spid->queue_cmd_msgs();

  // Show existing content
  screen->update();

  // Process received messages
  std::vector<MediaContainer*> new_content = spid->process_msgs();
  for (size_t i = 0;i < new_content.size(); ++i) {
    screen->enqueue(new_content[i]);
  }

  delay(10);
}
