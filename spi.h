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
    BACKLIGHT_ON = 0x0A,
    BACKLIGHT_OFF = 0x0B,
    ACK = 0xEF,
    ERROR = 0xEE
};

// Enums for Error Codes
enum class ErrorCode : uint8_t {
    SUCCESS = 0x00,
    UNKNOWN_MSG_TYPE = 0x01,
    INVALID_FORMAT = 0x02,
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
    // void send_ack(uint8_t message_id, ErrorCode status_code);
    // void send_error(uint8_t message_id, ErrorCode error_code, const std::string& error_msg);

    // Parsing Functions
    
    MediaContainer* handle_text_batch(uint8_t* payload, size_t payload_length, uint8_t message_id);
    // MediaContainer* handle_image_transfer_start(uint8_t* payload, size_t payload_length, uint8_t message_id);
    // MediaContainer* handle_image_chunk(uint8_t* payload, size_t payload_length, uint8_t message_id);
    // MediaContainer* handle_image_transfer_end(uint8_t* payload, size_t payload_length, uint8_t message_id);
    // MediaContainer* handle_option_list(uint8_t* payload, size_t payload_length, uint8_t message_id);
    // MediaContainer* handle_option_selection_update(uint8_t* payload, size_t payload_length, uint8_t message_id);
    // MediaContainer* handle_backlight_control(uint8_t* payload, size_t payload_length, uint8_t message_id);

    // Utility Functions
    uint16_t bytes_to_uint16(uint8_t high_byte, uint8_t low_byte);
    uint32_t bytes_to_uint32(uint8_t* bytes);
    void reset_expectations();

public:
    SPIDriver();
    MediaContainer* parse_message(uint8_t* buf, size_t buf_size);
    void queue_cmd_msgs();
    std::vector<MediaContainer*> process_msgs();
};


SPIDriver::SPIDriver()
    : dma_tx_buf(nullptr)
    , dma_rx_buf(nullptr)
    , expecting_image_id(0)
    , expecting_container(nullptr) {
    // Initialize SPI Slave
    dma_tx_buf = slave.allocDMABuffer(SPI_MISO_BUFFER_SIZE);
    dma_rx_buf = slave.allocDMABuffer(SPI_MOSI_BUFFER_SIZE);

    slave.setDataMode(SPI_MODE0);
    slave.setMaxTransferSize(SPI_MOSI_BUFFER_SIZE);
    slave.setQueueSize(QUEUE_SIZE);

    slave.begin();   // Default HSPI
}

void SPIDriver::queue_cmd_msgs() {
    if (slave.hasTransactionsCompletedAndAllResultsHandled()) {
        // Queue a new transaction
        slave.queue(dma_tx_buf, dma_rx_buf, SPI_MOSI_BUFFER_SIZE);
        slave.trigger();      // finally, we should trigger transaction in the background
    }
}

std::vector<MediaContainer*> SPIDriver::process_msgs() {
    std::vector<MediaContainer*> media_vec;

    if (slave.hasTransactionsCompletedAndAllResultsReady(QUEUE_SIZE)) {
        const std::vector<size_t> received_bytes = slave.numBytesReceivedAll();
        size_t len_counter = 0;

        // For each received message, deal with it and return a vector of containers
        for (auto buf_len : received_bytes) {
            MediaContainer* res = parse_message(dma_rx_buf + len_counter, buf_len);
            if (res != nullptr) media_vec.push_back(res);
            len_counter += buf_len;
        }
    }
    return media_vec;
}


uint16_t SPIDriver::bytes_to_uint16(uint8_t high_byte, uint8_t low_byte) {
    return (static_cast<uint16_t>(high_byte) << 8) | low_byte;
}

uint32_t SPIDriver::bytes_to_uint32(uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16)
         | (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
}

void SPIDriver::reset_expectations() {
    expecting_image_id = 0;
    expecting_container = nullptr;
}

// void SPIDriver::send_ack(uint8_t message_id, ErrorCode status_code) {
//     uint8_t ack_msg[7];
//     ack_msg[0] = SOF_MARKER;
//     ack_msg[1] = static_cast<uint8_t>(MessageType::ACK);
//     ack_msg[2] = message_id;
//     ack_msg[3] = 0x00;                                 // Payload Length High Byte
//     ack_msg[4] = 0x01;                                 // Payload Length Low Byte
//     ack_msg[5] = static_cast<uint8_t>(status_code);    // Status Code
//     ack_msg[6] = calculate_checksum(&ack_msg[1], 5);   // Checksum

//     // Send ACK message
//     slave.queue(ack_msg, nullptr, sizeof(ack_msg));

//     delete[] ack_msg;
// }

// void SPIDriver::send_error(uint8_t message_id, ErrorCode error_code, const std::string& error_msg) {
//     size_t error_msg_length = error_msg.size();
//     size_t total_length = 7 + error_msg_length;   // Header + Payload + Checksum

//     uint8_t* error_packet = new uint8_t[total_length];
//     error_packet[0] = SOF_MARKER;
//     error_packet[1] = static_cast<uint8_t>(MessageType::ERROR);
//     error_packet[2] = message_id;
//     error_packet[3] = 0x00;                                             // Payload Length High Byte
//     error_packet[4] = static_cast<uint8_t>(1 + 1 + error_msg_length);   // Payload Length Low Byte
//     error_packet[5] = static_cast<uint8_t>(error_code);
//     error_packet[6] = static_cast<uint8_t>(error_msg_length);

//     memcpy(&error_packet[7], error_msg.c_str(), error_msg_length);

//     error_packet[7 + error_msg_length] = calculate_checksum(&error_packet[1], 5 + error_msg_length);

//     // Send Error message
//     slave.queue(error_packet, nullptr, total_length);

//     delete[] error_packet;
// }

MediaContainer* SPIDriver::parse_message(uint8_t* buf, size_t buf_size) {
    if (buf[0] != SOF_MARKER) {
        // Invalid Start of Frame
        return show_debug_info("Invalid SOF Received");
    }
    MessageType message_type = static_cast<MessageType>(buf[1]);
    uint8_t message_id = buf[2];
    uint16_t payload_length = (buf[3] << 8) | buf[4];
    uint8_t* payload = buf+5;

    switch (message_type) {
    case MessageType::TEXT_BATCH:
        return handle_text_batch(payload, payload_length, message_id);
    // case MessageType::IMAGE_TRANSFER_START:
    //     return handle_image_transfer_start(payload, payload_length, message_id);
    // case MessageType::IMAGE_CHUNK:
    //     return handle_image_chunk(payload, payload_length, message_id);
    // case MessageType::IMAGE_TRANSFER_END:
    //     return handle_image_transfer_end(payload, payload_length, message_id);
    // case MessageType::OPTION_LIST:
    //     return handle_option_list(payload, payload_length, message_id);
    // case MessageType::OPTION_SELECTION_UPDATE:
    //     return handle_option_selection_update(payload, payload_length, message_id);
    // case MessageType::BACKLIGHT_CONTROL:
    //     return handle_backlight_control(payload, payload_length, message_id);
    default:
        // Unknown Message Type
        // send_error(message_id, ErrorCode::UNKNOWN_MSG_TYPE, "Unknown Message Type");
        return show_debug_info("Unknown Message Type!");
    }
}

MediaContainer* SPIDriver::handle_text_batch(uint8_t* payload, size_t payload_length, uint8_t message_id) {
    if (payload_length < 5) {
        return show_debug_info("String Length Less than 5!");
    }

    uint16_t bg_color = (payload[0] << 8) | payload[1];
    uint16_t font_color = (payload[2] << 8) | payload[3];
    uint8_t num_items = payload[4];
    size_t offset = 5;

    TextGroup* text_group = new TextGroup(0, bg_color, font_color);

    for (uint8_t i = 0; i < num_items; ++i) {
        if (offset + 6 > payload_length) {
            delete text_group;
            return show_debug_info("Not Enough Content for Text!");
        }

        uint16_t x_pos = bytes_to_uint16(payload[offset], payload[offset + 1]);
        uint16_t y_pos = bytes_to_uint16(payload[offset + 2], payload[offset + 3]);
        FontID font_id = static_cast<FontID>(payload[offset + 4]);
        uint8_t text_length = payload[offset + 5];

        if (offset + 6 + text_length > payload_length) {
            delete text_group;
            return show_debug_info("Text length exceeds payload!");
        }

        String text = String((char*) &payload[offset + 6], text_length);
        Text* txt = new Text(text, 0, font_id, x_pos, y_pos);
        text_group->add_member(txt);

        offset += 6 + text_length;
    }

    // send_ack(message_id, ErrorCode::SUCCESS);
    return text_group;
}

// MediaContainer* SPIDriver::handle_image_transfer_start(uint8_t* payload, size_t payload_length, uint8_t message_id) {
//     if (payload_length < 9) {
//         send_error(message_id, ErrorCode::INVALID_FORMAT, "Payload too short for Image Transfer Start");
//         return nullptr;
//     }

//     uint8_t image_id = payload[0];
//     ImageFormat image_format = static_cast<ImageFormat>(payload[1]);
//     ImageResolution image_resolution = static_cast<ImageResolution>(payload[2]);
//     uint32_t total_size = bytes_to_uint32(&payload[3]);
//     uint16_t delay_time = bytes_to_uint16(payload[6], payload[7]);

//     if (ongoing_transfers.find(image_id) != ongoing_transfers.end()) {
//         send_error(message_id, ErrorCode::IMAGE_ID_MISMATCH, "Image ID already in use");
//         return nullptr;
//     }

//     Image* image = new Image(image_id, image_format, image_resolution, total_size, 0);

//     if (image->get_status() == MediaStatus::EXPIRED) {
//         send_error(message_id, ErrorCode::OUT_OF_MEMORY, "Failed to allocate memory for image");
//         delete image;
//         return nullptr;
//     }

//     ongoing_transfers[image_id] = image;
//     send_ack(message_id, ErrorCode::SUCCESS);
//     return nullptr;
// }

// MediaContainer* SPIDriver::handle_image_chunk(uint8_t* payload, size_t payload_length, uint8_t message_id) {
//     if (payload_length < 3) {
//         send_error(message_id, ErrorCode::INVALID_FORMAT, "Payload too short for Image Chunk");
//         return nullptr;
//     }

//     uint8_t image_id = payload[0];
//     uint16_t chunk_number = bytes_to_uint16(payload[1], payload[2]);
//     size_t chunk_data_length = payload_length - 3;
//     uint8_t* chunk_data = &payload[3];

//     auto it = ongoing_transfers.find(image_id);
//     if (it == ongoing_transfers.end()) {
//         send_error(message_id, ErrorCode::IMAGE_ID_MISMATCH, "Image ID not found for chunk");
//         return nullptr;
//     }

//     Image* image = static_cast<Image*>(it->second);
//     image->add_chunk(chunk_data, chunk_data_length);

//     send_ack(message_id, ErrorCode::SUCCESS);
//     return nullptr;
// }

// MediaContainer* SPIDriver::handle_image_transfer_end(uint8_t* payload, size_t payload_length, uint8_t message_id) {
//     if (payload_length != 1) {
//         send_error(message_id, ErrorCode::INVALID_FORMAT, "Invalid payload length for Image Transfer End");
//         return nullptr;
//     }

//     uint8_t image_id = payload[0];
//     auto it = ongoing_transfers.find(image_id);
//     if (it == ongoing_transfers.end()) {
//         send_error(message_id, ErrorCode::IMAGE_ID_MISMATCH, "Image ID not found for transfer end");
//         return nullptr;
//     }

//     Image* image = static_cast<Image*>(it->second);
//     ongoing_transfers.erase(it);

//     send_ack(message_id, ErrorCode::SUCCESS);
//     return image;   // Return the completed image for display
// }

// MediaContainer* SPIDriver::handle_option_list(uint8_t* payload, size_t payload_length, uint8_t message_id) {
//     if (payload_length < 2) {
//         send_error(message_id, ErrorCode::INVALID_FORMAT, "Payload too short for Option List");
//         return nullptr;
//     }

//     uint8_t selected_index = payload[0];
//     uint8_t num_options = payload[1];
//     size_t offset = 2;

//     OptionGroup* option_group = new OptionGroup(selected_index);

//     for (uint8_t i = 0; i < num_options; ++i) {
//         if (offset >= payload_length) {
//             send_error(message_id, ErrorCode::INVALID_FORMAT, "Insufficient data for option");
//             delete option_group;
//             return nullptr;
//         }

//         uint8_t option_length = payload[offset];
//         if (offset + 1 + option_length > payload_length) {
//             send_error(message_id, ErrorCode::INVALID_FORMAT, "Option length exceeds payload");
//             delete option_group;
//             return nullptr;
//         }

//         String option_text = String((char*) &payload[offset + 1], option_length);
//         option_group->add_option(option_text);

//         offset += 1 + option_length;
//     }

//     send_ack(message_id, ErrorCode::SUCCESS);
//     return option_group;
// }

// MediaContainer* SPIDriver::handle_option_selection_update(uint8_t* payload, size_t payload_length, uint8_t message_id) {
//     if (payload_length != 1) {
//         send_error(message_id, ErrorCode::INVALID_FORMAT, "Invalid payload length for Option Selection Update");
//         return nullptr;
//     }

//     uint8_t selected_index = payload[0];

//     // Update the current option group if available
//     // Assuming there's a current_option_group variable or method to handle this
//     // current_option_group->set_selected_index(selected_index);

//     send_ack(message_id, ErrorCode::SUCCESS);
//     return nullptr;
// }

// MediaContainer* SPIDriver::handle_backlight_control(uint8_t* payload, size_t payload_length, uint8_t message_id) {
//     if (payload_length != 1) {
//         send_error(message_id, ErrorCode::INVALID_FORMAT, "Invalid payload length for Backlight Control");
//         return nullptr;
//     }

//     uint8_t state = payload[0];

//     // Control the backlight accordingly
//     // Assuming there's a function to set backlight
//     // set_backlight(state == 0x01);

//     send_ack(message_id, ErrorCode::SUCCESS);
//     return new MediaContainer(MediaType::BACKLIGHT_CONTROL, 0);
// }

} // namespace dice

#endif
