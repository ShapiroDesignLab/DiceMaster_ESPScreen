#ifndef DICE_SPI
#define DICE_SPI

#include <vector>
#include <string>

#include <ESP32DMASPISlave.h>
// #include <ESP32SPISlave.h>

#include "Media.h"

namespace dice {

// SPI Buffer Sizes
constexpr size_t SPI_MOSI_BUFFER_SIZE = 2048;  // Adjust as needed
constexpr size_t SPI_MISO_BUFFER_SIZE = 256;   // For ACK/NACK messages
constexpr size_t QUEUE_SIZE = 1;

// Enums for Message Types
enum class MessageType : uint8_t {
    TEXT_BATCH = 0x01,
    IMAGE_TRANSFER_START = 0x02,
    IMAGE_CHUNK = 0x03,
    IMAGE_TRANSFER_END = 0x04,
    OPTION_LIST = 0x05,
    OPTION_SELECTION_UPDATE = 0x06,
    GIF_TRANSFER_START = 0x07,
    GIF_FRAME = 0x08,
    GIF_TRANSFER_END = 0x09,
    BACKLIGHT_CONTROL = 0x0A,
    ACK = 0x0B,
    ERROR = 0x0C
};

// Enums for Error Codes
enum class ErrorCode : uint8_t {
    SUCCESS = 0x00,
    UNKNOWN_MSG_TYPE = 0x01,
    INVALID_FORMAT = 0x02,
    CHECKSUM_ERROR = 0x03,
    IMAGE_ID_MISMATCH = 0x04,
    PAYLOAD_LENGTH_MISMATCH = 0x05,
    UNSUPPORTED_IMAGE_FORMAT = 0x06,
    OUT_OF_MEMORY = 0x07,
    INTERNAL_ERROR = 0x08,
    INVALID_OPTION_INDEX = 0x09
};

// SPI Protocol constants
constexpr uint8_t SOF_MARKER = 0x7E;

// // Image Header info
// static const uint8_t IMG_HEADER_LEN = 4;            // CMD, nChunks, resolution, enforced time

// // For Text
// static const uint8_t TEXTGROUP_HEADER_LEN = 4;      // enforced time, color
// static const uint8_t TEXT_CHUNK_HEADER_LEN = 6;     // x_hi, x_lo, y_hi, y_lo, font, size

// // For Options
// static const uint8_t OPTION_HEADER_LEN = 2;         // CMD, selecting
// static const uint8_t OPTION_CHUNK_HEADER_LEN = 5;   // x_hi, x_lo, y_hi, y_lo, size

class SPIDriver {
private:
  ESP32DMASPI::Slave slave;
  uint8_t* dma_tx_buf;
  uint8_t* dma_rx_buf;

  // Variables for context management
  uint8_t expecting_bufs;
  MediaContainer* expecting_container;
  std::map<uint8_t, MediaContainer*> ongoing_transfers;

  // Acknowledgment and Error Handling
  void send_ack(uint8_t message_id, ErrorCode status_code);
  void send_error(uint8_t message_id, ErrorCode error_code, const std::string& error_msg);

  MediaContainer* pop_expect() {
    MediaContainer* rtn = expecting_container;
    expecting_bufs = 0;
    expecting_container = nullptr;
    return rtn;
  }

  // Chunk is defined with first 4 bytes being 255
  bool is_chunk(uint8_t* buf, size_t buf_size) {
    return (buf_size >= 4 
            && buf[0] == 255 
            && buf[1] == 255 
            && buf[2] == 255 
            && buf[3] == 255);
  }

  MediaContainer* parse(uint8_t* buf, size_t buf_size) {
    // All messages must be at least 4 bytes in length
    if (buf_size < 4){   
      return nullptr;
    }
    // Make sure first four bytes is same, indicating a 
    if ((buf[1] != buf[0]) || (buf[2] != buf[0]) || (buf[3] != buf[0])) {
      return nullptr;
    }

    // If expecting image, just push
    if (expecting_bufs > 0) {
      if (is_chunk(buf, buf_size)){
        switch (expecting_container->get_media_type()) {
          case MEDIA_IMAGE:   // If awaiting image, add to image
            return add2img(buf, buf_size);
          default:          // Other container types are not supported for now
            break;
        }
      }
      else { pop_expect(); }
    }

    // Otherwise, this is a message header, parse it
    switch (buf[0]) {
      case CMD_PING: 
        last_ping_time = millis();
        return new Text("Received Ping command");
        return nullptr;
      case CMD_NEW_IMG:
        return new Text("Received Image Command");
        return make_img(buf+4, buf_size-4);
      case CMD_TEXT:
        return new Text("Received Text Command");
        return make_txt_group(buf+4, buf_size-4);
      case CMD_OPTIONS:
        return new Text("Received Option command");
        return make_options(buf+4, buf_size-4);
      case CMD_OPTIONS_END:
        return new Text("Received Option End command");
        return new MediaContainer(MEDIA_OPTION_END, 0);
      case CMD_BACKLIGHT_ON:
        return new Text("Received Backlight on");
        return new MediaContainer(MEDIA_BACKLIGHT_ON, 0);
      case CMD_BACKLIGHT_OFF:
        return new Text("Received Backlight off");
        return new MediaContainer(MEDIA_BACKLIGHT_OFF, 0);
    }
  }

  // Image parsing functions
  MediaContainer* add2img(uint8_t* buf, size_t buf_size) {
    assert(expecting_container != nullptr);
    expecting_container->add_chunk(dma_rx_buf, buf_size);
    if (expecting_container->get_status() >= STATUS_DECODING) {
      return pop_expect();
    }
    return nullptr;
  }

  MediaContainer* make_img(uint8_t* buf, size_t buf_size) {
    assert(!expecting_bufs);
    expecting_bufs = buf[1];
    expecting_container = new Image(buf[2], buf[3], buf[4]);
    if (buf_size > 5) {       // If there is payload immediately after, add those
      add2img(buf+5, buf_size-5);
    }
    return nullptr;
  }

  // Text parsing functions
  MediaContainer* make_txt_group(uint8_t* buf, size_t buf_size) {
    // buf here is free of command header
    if (buf_size < 4) return nullptr;
    // If an image transfer is in progress, ditch results
    if (expecting_bufs) pop_expect();

    // Parse text transfer message
    const uint8_t up_time = buf[0];
    MediaContainer* txt_group = new TextGroup(up_time, RGB565(buf[1], buf[2], buf[3]));

    // Parse each text group
    uint8_t* payload = buf + TEXTGROUP_HEADER_LEN;   // First 4 bytes are header info
    buf_size -= TEXTGROUP_HEADER_LEN;

    while (buf_size > TEXT_CHUNK_HEADER_LEN) {        // At least 6 headers with 1 actual word
      // Unpack header content
      const uint16_t x_pos = payload[0]*255 + payload[1];
      const uint16_t y_pos = payload[2]*255 + payload[3];
      const uint8_t* font = map_font(payload[4]);
      const uint8_t len_msg = payload[5];
      payload += TEXT_CHUNK_HEADER_LEN;
      buf_size -= TEXT_CHUNK_HEADER_LEN;

      // Add to container
      assert(payload[TEXT_CHUNK_HEADER_LEN-1] == 0);      // Last byte of char must be zero  
      MediaContainer* txt = new Text((char*)payload+TEXT_CHUNK_HEADER_LEN, up_time, font, x_pos, y_pos);
      txt_group->add_member(txt);
      
      // Deal with Next Chunk
      payload += len_msg;
      buf_size -= len_msg;
    }
  }

  // Options parsing functions
  MediaContainer* make_options(uint8_t* buf, size_t buf_size) {
    assert(!expecting_bufs);
    MediaContainer* txt_group = new OptionGroup(buf[1]);

    // Parse each text group
    uint8_t* payload = buf + OPTION_HEADER_LEN;   // First 2 bytes are header info
    buf_size -= OPTION_HEADER_LEN;

    // while (buf_size >= OPTION_CHUNK_HEADER_LEN) {        // At least 6 headers, 1 byte actual word, 1 byte stop word
    //   if (payload[5] > 1) {       // At least 1 byte actual word, 1 byte stop word
    //     MediaContainer* txt = new Text((char*)payload+5, 0, u8g2_font_unifont_tf
    //                                 , (uint16_t) payload[0]*255 + payload[1]
    //                                 , (uint16_t) payload[2]*255 + payload[3]);
    //     assert(payload[OPTION_CHUNK_HEADER_LEN + payload[4]] == '\0');      // Last byte of char must be zero      
    //     txt_group->add_member(txt);
    //   }
    //   // Deal with Next Chunk
    //   size_t txt_chunk_len = payload[4] + OPTION_CHUNK_HEADER_LEN;
    //   payload += txt_chunk_len;
    //   buf_size -= txt_chunk_len;
    // }
  }

public:
  SPIDriver() 
    : dma_tx_buf(slave.allocDMABuffer(SPI_MISO_BUFFER_SIZE))
    , dma_rx_buf(slave.allocDMABuffer(SPI_MOSI_BUFFER_SIZE))
    , expecting_bufs(0)
    , expecting_container(nullptr)
    , last_ping_time(0)
    {
      slave.setDataMode(SPI_MODE0);
      slave.setMaxTransferSize(SPI_MOSI_BUFFER_SIZE);
      slave.setQueueSize(QUEUE_SIZE);

      slave.begin();  // default: HSPI (please refer README for pin assignments)
    }

  void queue_cmd_msgs() {
    // if no transaction is in flight and all results are handled, queue new transactions
    if (slave.hasTransactionsCompletedAndAllResultsHandled()) {
      // do some initialization for tx_buf and rx_buf
      slave.queue(NULL, dma_rx_buf, 1024);
      slave.trigger();      // finally, we should trigger transaction in the background
    }
  }

  std::vector<MediaContainer*> process_msgs() {   // Update function to be called in 
    std::vector<MediaContainer*> vec;
    // if all transactions are completed and all results are ready, handle results
    if (slave.hasTransactionsCompletedAndAllResultsReady(QUEUE_SIZE)) {
      const std::vector<size_t> received_bytes = slave.numBytesReceivedAll();
      size_t len_counter = 0;


      /********
      DEBUG ONLY START
      ********/
      String msg1 = "Received messages ";
      msg1 += received_bytes.size();
      String msg2 = "First one length ";
      msg2 += received_bytes[0];
      TextGroup* group = new TextGroup(0, RED);

      String content = " ";
      for (size_t i = 0;i < 32;i++) {
        content += dma_rx_buf[i];
        content += " ";
      }
      group->add_member(new Text(msg1, 0, u8g2_font_unifont_tf, 40, 40));
      group->add_member(new Text(msg2, 0, u8g2_font_unifont_tf, 40, 120));
      group->add_member(new Text(content, 0, u8g2_font_unifont_tf, 40, 180));
      vec.push_back(group);
      /********
      DEBUG ONLY END
      ********/


      // For each received message, deal with it and return a vector of containers
      for (auto buf_len : received_bytes) {
        MediaContainer* res = parse(dma_rx_buf + len_counter, buf_len);
        if (res != nullptr) vec.push_back(res);
        len_counter += buf_len;
      }
    }
    Serial.println(vec.size());
    return vec;
  }
};

} // namespace dice

#endif
