#include "screen.h"
#include "spi.h"

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


// SUCCESS!!!
uint8_t* make_test_text_message(){
  // SOF, Text_msg, msgid=1, payload_len=18
  uint8_t header[5] = {0x7E, 0x01, 0x01, 0x00, 0x12};
  // bg_color = 0xF79E, txt color = 0x0861
  uint8_t group_color[4] = {0xF7, 0x9E, 0x08, 0x61};
  uint8_t num_lines = 1;
  // x=28, y=28, font=3(Chinese), length=7(bytes)
  uint8_t text1_header[6] = {0x00, 0x28, 0x00, 0x28, 0x03, 0x07};
  // text utf-8 bytes
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

void num2bytes(uint32_t size, uint8_t k, uint8_t* buffer) {
  // MSB first
  for (uint8_t i = 0;i < k;i++) {
    uint32_t factor = pow(2, 8*(k-i-1));
    buffer[i] = size / factor;
    size %= factor;
  }
  return;
}

// uint8_t* make_test_img_message(){
//   // Setup global stuff
//   uint8_t msg_id = 0;
//   uint8_t num_msgs = ceil(umlogo_sq240_SIZE/65535) + 2;
//   uint8_t* msgs[] = new uint8_t[num_msgs];

//   // Start Frame
//   // SOF, IMG_start_msg, msgid=0, payload_len=9
//   uint8_t s_header[5] = {0x7E, 0x02, msg_id++, 0x00, 0x09};

//   uint8_t img_id = 32;
//   uint32_t max_chunk_size = 1024;

//   // imageId=1, JPEG240, delay=511, content_size=umlogo_sq240_SIZE, num_chunks
//   uint8_t s_content[8] = {img_id, 0x11, 0x01, 0xFF, 0x00, 0x00, 0x00, ceil(umlogo_sq240_SIZE/max_chunk_size));
//   num2bytes(umlogo_sq240_SIZE, 3, (&s_content)+4);

//   // Header
//   uint8_t* header_arr = new uint8_t[13];
//   memcpy(header_arr, s_header, 5);
//   memcpy(header_arr+5, s_content, 8);
//   msgs[msg_id] = header_arr;
//   msg_id++;

//   // Image Chunks
//   for (uint32_t i = 0;i < umlogo_sq240_SIZE;i+=max_chunk_size) {
//     uint32_t chunk_size = min(max_chunk_size, umlogo_sq240-i);
//     // SOF, IMG_start_msg, msgid=2, payload_len=9
//     uint8_t c_header[5] = {0x7E, 0x03, msg_id++, 0x00, 0x09};
    
//     // SOF, IMG_start_msg, msgid=2, payload_len=9
//     uint8_t c_chunk_header[7] = {msg_id, msg_id-1, 0x0, 0x0, 0x0, 0x00, 0x00};
//     num2bytes(i, 3, (&c_header)+2);
//     num2bytes(chunk_size, 2, (&c_header)+5);

//     uint8_t* chunk_arr = new uint8_t[chunk_size+7];
//     memcpy(chunk_arr, s_header, 7);
//     memcpy(chunk_arr+7, umlogo_sq240+i, chunk_size);
//     msgs[msg_id] = chunk_arr;

//     msg_id++;
//   }

//   // End Frame
//   // SOF, IMG_start_msg, msgid=2, payload_len=9
//   uint8_t e_header[2] = {0x7E, 0x04, msg_id};
//   // imageId=1, 
//   uint8_t s_content[8] = {0x01, 0x11, 0x01, 0xFF, 0x00, 0x00, 0x00, ceil(umlogo_sq240_SIZE/65535));
//   num2bytes(umlogo_sq240_SIZE, 3, (&s_content)+4);
//   uint8_t* header_arr = new uint8_t[13];
//   memcpy(header_arr, s_header, 5);
//   memcpy(header_arr+5, s_content, 8);
//   msgs[msg_id] = header_arr;
//   msg_id++;
// }

void loop()
{
  /*******************
  BEGIN DEMO ONLY
  *******************/
  // Draw image
  // screen->draw_startup_logo();
  // screen->update();
  // delay(1000); 

  // Draw Revolving Logo
  screen->draw_revolving_logo();
  screen->update();

  // Draw Text To Debug Parser
  // uint8_t* msg = make_test_text_message();
  // screen->enqueue(spid->parse_message(msg, 23));
  // delete msg;
  // screen->update();
  // delay(1000); 
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
