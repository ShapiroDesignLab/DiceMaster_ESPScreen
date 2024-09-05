#ifndef DICE_SPI
#define DICE_SPI

#include <vector>
#include <string>

#include <ESP32DMASPISlave.h>
// #include <ESP32SPISlave.h>

#include "Media.h"

namespace dice {

static const size_t SPI_MOSI_BUFFER_SIZE = 1024;  // 2k buffer
static const size_t SPI_MISO_BUFFER_SIZE = 1024;    // probably not using for now
static const size_t QUEUE_SIZE = 1;

static const uint8_t CMD_PING = 1;
static const uint8_t CMD_NEW_IMG = 3;
static const uint8_t CMD_TEXT = 31;
static const uint8_t CMD_OPTIONS = 63;
static const uint8_t CMD_OPTIONS_END = 64;
static const uint8_t CMD_BACKLIGHT_ON = 253;
static const uint8_t CMD_BACKLIGHT_OFF = 254;

// Image Header info
static const uint8_t IMG_HEADER_LEN = 4;            // CMD, nChunks, resolution, enforced time

// For Text
static const uint8_t MIN_TXT_LEN = 2;
static const uint8_t TEXTGROUP_HEADER_LEN = 5;      // CMD, enforced time, color
static const uint8_t TEXT_CHUNK_HEADER_LEN = 6;     // x_hi, x_lo, y_hi, y_lo, font, size

// For Options
static const uint8_t OPTION_HEADER_LEN = 2;         // CMD, selecting
static const uint8_t OPTION_CHUNK_HEADER_LEN = 5;   // x_hi, x_lo, y_hi, y_lo, size

// SPI Protocol related


class SPIDriver {
private:
  ESP32DMASPI::Slave slave;
  uint8_t* dma_tx_buf;
  uint8_t* dma_rx_buf;

  // ESP32SPISlave slave;
  // uint8_t dma_tx_buf[SPI_MISO_BUFFER_SIZE];
  // uint8_t dma_rx_buf[SPI_MOSI_BUFFER_SIZE] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};

  // Variables for context management
  uint8_t expecting_bufs;
  MediaContainer* expecting_container;

  // Pinging message
  size_t last_ping_time;

  MediaContainer* pop_expect() {
    MediaContainer* rtn = expecting_container;
    expecting_bufs = 0;
    expecting_container = nullptr;
    return rtn;
  }

  // Chunk is defined with first 4 bytes being 255
  bool is_chunk(uint8_t* buf, size_t buf_size) {
    return (buf_size >= 4 && buf[0] == 255 && buf[1] == 255 && buf[2] == 255 && buf[3] == 255);
  }

  MediaContainer* parse(uint8_t* buf, size_t buf_size) {
    if (buf_size < 4){   // All messages must be at least 4 bytes in length
      return nullptr;
    }

    // If expecting image, just push
    if (expecting_bufs > 0) {
      assert(expecting_container != nullptr);
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
        Serial.println("Received Ping");
        return nullptr;
        last_ping_time = millis();
        return nullptr;
      case CMD_NEW_IMG:
        Serial.println("Received image Header");
        return nullptr;
        return make_img(buf, buf_size);
      case CMD_TEXT:
        Serial.println("Received Text");
        return nullptr;
        return make_txt_group(buf, buf_size);
      case CMD_OPTIONS:
        Serial.println("Received Options");
        return nullptr;
        return make_options(buf, buf_size);
      case CMD_OPTIONS_END:
        Serial.println("Received Option End");
        return nullptr;
        return new MediaContainer(MEDIA_OPTION_END, 0);
      case CMD_BACKLIGHT_ON:
        Serial.println("Received BL On");
        return nullptr;
        return new MediaContainer(MEDIA_BACKLIGHT_ON, 0);
      case CMD_BACKLIGHT_OFF:
        Serial.println("Received BL Off");
        return nullptr;
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
    if (buf_size > 5) {       // If there is payload immediiately after, add those
      add2img(buf+5, buf_size-5);
    }
    return nullptr;
  }

  // Text parsing functions
  MediaContainer* make_txt_group(uint8_t* buf, size_t buf_size) {
    assert(!expecting_bufs);
    uint8_t up_time = buf[1];
    MediaContainer* txt_group = new TextGroup(up_time, RGB565(buf[2], buf[3], buf[4]));

    // Parse each text group
    uint8_t* payload = buf + TEXTGROUP_HEADER_LEN;   // First 4 bytes are header info
    buf_size -= TEXTGROUP_HEADER_LEN;

    while (buf_size >= TEXT_CHUNK_HEADER_LEN + MIN_TXT_LEN) {        // At least 6 headers, 1 byte actual word, 1 byte stop word
      if (payload[5] > 1) {       // At least 1 byte actual word, 1 byte stop word
        MediaContainer* txt = new Text((char*)payload+TEXT_CHUNK_HEADER_LEN, up_time, map_font(payload[4])
                                    , (uint16_t) payload[0]*255 + payload[1]
                                    , (uint16_t) payload[2]*255 + payload[3]);
        assert(payload[TEXT_CHUNK_HEADER_LEN + payload[5]] == '\0');      // Last byte of char must be zero      
        txt_group->add_member(txt);
      }
      // Deal with Next Chunk
      size_t txt_chunk_len = payload[5] + TEXT_CHUNK_HEADER_LEN;
      payload += txt_chunk_len;
      buf_size -= txt_chunk_len;
    }
  }

  // Options parsing functions
  MediaContainer* make_options(uint8_t* buf, size_t buf_size) {
    assert(!expecting_bufs);
    MediaContainer* txt_group = new OptionGroup(buf[1]);

    // Parse each text group
    uint8_t* payload = buf + OPTION_HEADER_LEN;   // First 2 bytes are header info
    buf_size -= OPTION_HEADER_LEN;

    while (buf_size >= OPTION_CHUNK_HEADER_LEN + MIN_TXT_LEN) {        // At least 6 headers, 1 byte actual word, 1 byte stop word
      if (payload[5] > 1) {       // At least 1 byte actual word, 1 byte stop word
        MediaContainer* txt = new Text((char*)payload+5, 0, u8g2_font_unifont_tf
                                    , (uint16_t) payload[0]*255 + payload[1]
                                    , (uint16_t) payload[2]*255 + payload[3]);
        assert(payload[OPTION_CHUNK_HEADER_LEN + payload[4]] == '\0');      // Last byte of char must be zero      
        txt_group->add_member(txt);
      }
      // Deal with Next Chunk
      size_t txt_chunk_len = payload[4] + OPTION_CHUNK_HEADER_LEN;
      payload += txt_chunk_len;
      buf_size -= txt_chunk_len;
    }
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
      // slave.setMaxTransferSize(SPI_MOSI_BUFFER_SIZE);
      slave.setQueueSize(QUEUE_SIZE);

      // begin() after setting
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

      // String msg1 = "Received messages ";
      // msg1 += received_bytes.size();
      // String msg2 = "First one length ";
      // msg2 += received_bytes[0];
      // TextGroup* group = new TextGroup(0, RED);

      // String content = " ";
      // for (size_t i = 0;i < 32;i++) {
      //   content += dma_rx_buf[i];
      //   content += " ";
      // }
      // group->add_member(new Text(msg1, 0, u8g2_font_unifont_tf, 40, 40));
      // group->add_member(new Text(msg2, 0, u8g2_font_unifont_tf, 40, 120));
      // group->add_member(new Text(content, 0, u8g2_font_unifont_tf, 40, 180));
      // vec.push_back(group);

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