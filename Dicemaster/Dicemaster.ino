#include "screen.h"
#include "spi.h"
#include "jpg.hs/umlogo_sq240.h"
using namespace dice;

Screen* screen;
SPIDriver* spid;

void setup(void)
{
  // // Init serial
  Serial.begin(115200);
  Serial.println("Begin TransSssions");

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

  // Draw Revolving Logo
  screen->draw_revolving_logo();
  screen->update();

  // Decode Image From Scratch Test
  uint8_t** msgs = make_test_img_message(
    umlogo_sq240,
    umlogo_sq240_SIZE,
    31,
    1024,
    0
  );
  Serial.println("Made Image Msgs");
  for (int i=0;i<sizeof(msgs);i++) {
    try {
      uint8_t* msg = msgs[i];
      MediaContainer* rtn = spid->parse_message(msg, sizeof(msg));
      if (rtn != nullptr) {
        screen->enqueue(rtn);
      }
      Serial.println("Parsed Msg");
      delete[] msg;
      delay(500); 
    }
    catch (...) {
      String err_msg = "Startup Logo Decoding Failed";
      MediaContainer* err = print_error(err_msg);
      screen->enqueue(err);
      Serial.println(err_msg);
    }
  }
  delete[] msgs;
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

  delay(1);
}
