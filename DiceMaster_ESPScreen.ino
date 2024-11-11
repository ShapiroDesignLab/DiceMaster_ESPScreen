#include "screen.h"
#include "spi.h"

using namespace dice;

Screen* screen;
SPIDriver* spid;

void setup(void)
{
  // // Init serial
  Serial.begin(115200);
  Serial.println("Begin Transmissions");

  // Check PSRAM Init Status
  if (psramInit()) Serial.println("\nPSRAM correctly initialized");
  else Serial.println("PSRAM not available");

  spid = new SPIDriver();
  screen = new Screen();
}


// SUCCESS!!!
uint8_t* make_test_text_message(){
  uint8_t header[5] = {0x7E, 0x01, 0x01, 0x00, 0x14};
  uint8_t group_color[4] = {0xF7, 0x9E, 0x08, 0x61};
  uint8_t num_lines = 1;
  uint8_t text1_header[6] = {0x00, 0x28, 0x00, 0x28, 0x03, 0x07};
  uint8_t text_bytes[7] = {0xE4, 0xBD, 0xA0, 0xE5, 0xA5, 0xBD, 0x00};
  uint8_t* msg_arr = new uint8_t[23];
  uint8_t* ptr = msg_arr;
  memcpy(ptr, header, 5);
  ptr += 5;
  memcpy(ptr, group_color, 4);
  ptr += 4;
  *ptr = num_lines;
  ptr += 1;
  memcpy(ptr, text1_header, 6);
  ptr += 6;
  memcpy(ptr, text_bytes, 7);
  return msg_arr;
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

  // Draw Text To Debug Parser
  // screen->enqueue(get_demo_textgroup());
  uint8_t* msg = make_test_text_message();
  screen->enqueue(spid->parse_message(msg, 23));
  delete msg;
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
