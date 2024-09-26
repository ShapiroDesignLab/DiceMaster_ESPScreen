#ifndef DICE_SPI
#define DICE_SPI

#include <vector>
#include <string>
#include <map>

#include <ESP32DMASPISlave.h>
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

class SPIDriver {
private:
    ESP32DMASPI::Slave slave;
    uint8_t* dma_tx_buf;
    uint8_t* dma_rx_buf;

    // Context Management
    uint8_t expecting_image_id;
    MediaContainer* expecting_container;
    std::map<uint8_t, MediaContainer*> ongoing_transfers;

    // Acknowledgment and Error Handling
    void send_ack(uint8_t message_id, ErrorCode status_code);
    void send_error(uint8_t message_id, ErrorCode error_code, const std::string& error_msg);

    // Parsing Functions
    MediaContainer* parse_message(uint8_t* buf, size_t buf_size);
    MediaContainer* handle_text_batch(uint8_t* payload, size_t payload_length, uint8_t message_id);
    MediaContainer* handle_image_transfer_start(uint8_t* payload, size_t payload_length, uint8_t message_id);
    MediaContainer* handle_image_chunk(uint8_t* payload, size_t payload_length, uint8_t message_id);
    MediaContainer* handle_image_transfer_end(uint8_t* payload, size_t payload_length, uint8_t message_id);
    MediaContainer* handle_option_list(uint8_t* payload, size_t payload_length, uint8_t message_id);
    MediaContainer* handle_option_selection_update(uint8_t* payload, size_t payload_length, uint8_t message_id);
    MediaContainer* handle_backlight_control(uint8_t* payload, size_t payload_length, uint8_t message_id);

    // Utility Functions
    uint8_t calculate_checksum(uint8_t* data, size_t length);
    bool validate_checksum(uint8_t* buf, size_t buf_size);
    uint16_t bytes_to_uint16(uint8_t high_byte, uint8_t low_byte);
    uint32_t bytes_to_uint32(uint8_t* bytes);
    void reset_expectations();

public:
    SPIDriver();
    void queue_cmd_msgs();
    std::vector<MediaContainer*> process_msgs();
};

} // namespace dice

#endif
