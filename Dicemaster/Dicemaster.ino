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

// Decompose a multi-byte integer into multiple bytes of data, and write to buffer.
// k: how many bytes to decompose. 
void num2bytes(uint32_t size, uint8_t k, uint8_t* buffer) {
  // MSB first
  for (uint8_t i = 0;i < k;i++) {
    uint32_t factor = pow(2, 8*(k-i-1));
    buffer[i] = size / factor;
    size %= factor;
  }
  return;
}
uint8_t* concat_two_arrays(
      const uint8_t* a, 
      size_t len_a, 
      const uint8_t* b, 
      size_t len_b
  ) {
  size_t total_len = len_a + len_b;
  uint8_t* result = new uint8_t[total_len];

  memcpy(result, a, len_a);
  memcpy(result+len_a, b, len_b);
  return result;  // caller must delete[] this
}

// SUCCESS!!!
uint8_t* make_test_text_message() {
  // SOF, Text_msg, msgid=1, payload_len=18
  // uint8_t* header = make_msg_header(MessageType::TEXT_BATCH, 0x01, )
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

uint8_t* make_msg_header(MessageType msg_type, uint8_t msg_id, size_t payload_length) {
    uint8_t* header = new uint8_t[5];  // Allocate on the heap
    header[0] = 0x7E;
    header[1] = static_cast<uint8_t>(msg_type);
    header[2] = msg_id;
    num2bytes(payload_length, 2, header + 3);  // Write 2-byte length starting at header[3]
    return header;  // Return heap-allocated array
}

uint8_t* make_textgroup_header(uint16_t bg_color, uint16_t font_color, uint8_t num_lines) {
  uint8_t* tg_header = new uint8_t[5];
  num2bytes(bg_color, 2, tg_header);
  num2bytes(font_color, 2, tg_header+2);
  tg_header[4] = num_lines;
  return tg_header;
}

uint8_t* make_text_header(uint16_t x_cursor, uint16_t y_cursor, uint8_t font_id, uint8_t text_length) {
  uint8_t* t_header = new uint8_t[6];
  num2bytes(x_cursor, 2, t_header);
  num2bytes(y_cursor, 2, t_header+2);
  t_header[4] = font_id;
  t_header[5] = text_length;
  return t_header;
}

uint8_t* make_imgstart_header(
      uint8_t img_id, 
      ImageFormat format, 
      ImageResolution resolution, 
      uint8_t delay_time, 
      size_t img_size, 
      uint8_t num_chunks
  ) {
  // start meta header
  uint8_t* ib_header = new uint8_t[7];
  ib_header[0] = img_id % 256;
  uint8_t byte1 = static_cast<uint8_t>(resolution);
  byte1 |= static_cast<uint8_t>(format) << 4;
  ib_header[1] = byte1;
  ib_header[2] = delay_time;
  num2bytes(img_size, 3, ib_header+3);
  ib_header[6] = num_chunks;
  return ib_header;
}

uint8_t* make_imgchunk_header(
    uint8_t img_id,
    uint8_t chunk_id,
    uint32_t starting_location,
    uint16_t chunk_length
  ) {
  uint8_t* ic_header = new uint8_t[7];
  ic_header[0] = img_id % 256;
  ic_header[1] = chunk_id;
  num2bytes(starting_location, 3, ic_header+2);
  num2bytes(chunk_length, 2, ic_header+5);
  return ic_header;
}

uint8_t* make_imgend_header(
    uint8_t img_id
  ) {
  uint8_t* ie_header = new uint8_t[1];
  ie_header[0] = img_id % 256;
  return ie_header;
}

// img = umlogo_sq240
// img_size = umlogo_sq240_SIZE
// img_id = 31
// max_chunk_size = 1024
// first_msg_id = 0
uint8_t** make_test_img_message(
  const uint8_t* img,
  const size_t img_size,
  const uint8_t img_id,
  const size_t max_chunk_size,
  const uint8_t first_msg_id
){
  // Setup global stuff
  uint8_t msg_id = first_msg_id;
  uint8_t num_chunks = ceil(img_size/max_chunk_size);
  uint8_t num_msgs = num_chunks + 2; // start frame, payloads, end frame
  uint8_t** msgs = new uint8_t*[num_msgs];

  // Start Frame
  uint8_t msg_header_len = 5;
  uint8_t start_msg_len = 7;
  uint8_t* start_msg_header = make_msg_header(
      MessageType::IMAGE_TRANSFER_START, 
      msg_id, 
      start_msg_len
  );
  // Start Msg Content
  uint8_t* start_msg = make_imgstart_header(
    0x01, 
    ImageFormat::JPEG, 
    ImageResolution::SQ240, 
    250,
    umlogo_sq240_SIZE, 
    num_chunks
  );
  msgs[msg_id++] = concat_two_arrays(
        start_msg_header, 
        msg_header_len, 
        start_msg, 
        start_msg_len
  );
  delete[] start_msg_header;
  delete[] start_msg;

  // Image Chunks
  size_t chunk_start = 0;
  for (uint32_t i = 0;i < umlogo_sq240_SIZE;i+=max_chunk_size) {
    Serial.print("Chunk ");
    Serial.println(i);
    uint32_t chunk_size = min(max_chunk_size, umlogo_sq240_SIZE-i);
    uint8_t c_msg_len = 7;
    // Make msg header and chunk metadata
    uint8_t* c_msg_header = make_msg_header(
      MessageType::IMAGE_CHUNK, 
      msg_id, 
      c_msg_len+chunk_size
    );
    uint8_t* c_msg = make_imgchunk_header(
      img_id, 
      i, 
      chunk_start, 
      chunk_size
    );
    // Combine Headers
    uint8_t* chunk_header = concat_two_arrays(
      c_msg_header, 
      msg_header_len, 
      c_msg, 
      c_msg_len
    );
    delete[] c_msg_header;
    delete[] c_msg;
    // Then add the 
    msgs[msg_id++] = concat_two_arrays(
      chunk_header, 
      msg_header_len+c_msg_len, 
      img+chunk_start, 
      chunk_size
    );
    delete[] chunk_header;
    chunk_start += chunk_size;
  }

  // End Frame
  uint8_t end_msg_len = 1;
  uint8_t* end_msg_header = make_msg_header(
        MessageType::IMAGE_TRANSFER_END, 
        msg_id, 
        end_msg_len
  );
  uint8_t* end_msg = make_imgend_header(img_id);
  msgs[msg_id++] = concat_two_arrays(
    end_msg_header, 
    msg_header_len, 
    end_msg, 
    end_msg_len
  );
  delete[] end_msg_header;
  delete[] end_msg;
  return msgs;
}

void loop()
{
  /*******************
  BEGIN DEMO ONLY
  *******************/
  // Draw image
  // screen->draw_startup_logo();
  // screen->update();

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
