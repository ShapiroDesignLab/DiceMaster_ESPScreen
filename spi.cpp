#include <ESP32DMASPISlave.h>
#include "spi.h"

namespace dice {

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
        slave.clearTransaction();
    }
}

std::vector<MediaContainer*> SPIDriver::process_msgs() {
    std::vector<MediaContainer*> media_vec;

    if (slave.hasTransactionsCompletedAndAllResultsReady(QUEUE_SIZE)) {
        const std::vector<size_t> received_bytes = slave.numBytesReceivedAll();
        size_t len_counter = 0;

        // For each received message, deal with it and return a vector of containers
        for (auto buf_len : received_bytes) {
            MediaContainer* res = parse(dma_rx_buf + len_counter, buf_len);
            if (res != nullptr) vec.push_back(res);
            len_counter += buf_len;
        }
    }
    return media_vec;
}


uint8_t SPIDriver::calculate_checksum(uint8_t* data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; ++i) {
        checksum ^= data[i];
    }
    return checksum;
}

bool SPIDriver::validate_checksum(uint8_t* buf, size_t buf_size) {
    if (buf_size < 6) {
        // Minimum message size is 6 bytes
        return false;
    }
    uint16_t payload_length = (buf[3] << 8) | buf[4];
    size_t expected_size = 6 + payload_length;   // Header + Payload + Checksum

    if (buf_size != expected_size) {
        return false;
    }

    uint8_t calculated_checksum
      = calculate_checksum(&buf[1], 4 + payload_length);   // From Message Type to end of Payload
    uint8_t received_checksum = buf[5 + payload_length];

    return calculated_checksum == received_checksum;
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

void SPIDriver::send_ack(uint8_t message_id, ErrorCode status_code) {
    uint8_t ack_msg[7];
    ack_msg[0] = SOF_MARKER;
    ack_msg[1] = static_cast<uint8_t>(MessageType::ACK);
    ack_msg[2] = message_id;
    ack_msg[3] = 0x00;                                 // Payload Length High Byte
    ack_msg[4] = 0x01;                                 // Payload Length Low Byte
    ack_msg[5] = static_cast<uint8_t>(status_code);    // Status Code
    ack_msg[6] = calculate_checksum(&ack_msg[1], 5);   // Checksum

    // Send ACK message
    slave.queue(ack_msg, nullptr, sizeof(ack_msg));

    delete[] ack_msg;
}

void SPIDriver::send_error(uint8_t message_id, ErrorCode error_code, const std::string& error_msg) {
    size_t error_msg_length = error_msg.size();
    size_t total_length = 7 + error_msg_length;   // Header + Payload + Checksum

    uint8_t* error_packet = new uint8_t[total_length];
    error_packet[0] = SOF_MARKER;
    error_packet[1] = static_cast<uint8_t>(MessageType::ERROR);
    error_packet[2] = message_id;
    error_packet[3] = 0x00;                                             // Payload Length High Byte
    error_packet[4] = static_cast<uint8_t>(1 + 1 + error_msg_length);   // Payload Length Low Byte
    error_packet[5] = static_cast<uint8_t>(error_code);
    error_packet[6] = static_cast<uint8_t>(error_msg_length);

    memcpy(&error_packet[7], error_msg.c_str(), error_msg_length);

    error_packet[7 + error_msg_length] = calculate_checksum(&error_packet[1], 5 + error_msg_length);

    // Send Error message
    slave.queue(error_packet, nullptr, total_length);

    delete[] error_packet;
}

MediaContainer* SPIDriver::parse_message(uint8_t* buf, size_t buf_size) {
    if (buf[0] != SOF_MARKER) {
        // Invalid Start of Frame
        return nullptr;
    }

    if (!validate_checksum(buf, buf_size)) {
        // Checksum error
        uint8_t message_id = buf[2];
        send_error(message_id, ErrorCode::CHECKSUM_ERROR, "Checksum Error");
        return nullptr;
    }

    MessageType message_type = static_cast<MessageType>(buf[1]);
    uint8_t message_id = buf[2];
    uint16_t payload_length = (buf[3] << 8) | buf[4];
    uint8_t* payload = &buf[5];

    switch (message_type) {
    case MessageType::TEXT_BATCH:
        return handle_text_batch(payload, payload_length, message_id);
    case MessageType::IMAGE_TRANSFER_START:
        return handle_image_transfer_start(payload, payload_length, message_id);
    case MessageType::IMAGE_CHUNK:
        return handle_image_chunk(payload, payload_length, message_id);
    case MessageType::IMAGE_TRANSFER_END:
        return handle_image_transfer_end(payload, payload_length, message_id);
    case MessageType::OPTION_LIST:
        return handle_option_list(payload, payload_length, message_id);
    case MessageType::OPTION_SELECTION_UPDATE:
        return handle_option_selection_update(payload, payload_length, message_id);
    case MessageType::BACKLIGHT_CONTROL:
        return handle_backlight_control(payload, payload_length, message_id);
    default:
        // Unknown Message Type
        send_error(message_id, ErrorCode::UNKNOWN_MSG_TYPE, "Unknown Message Type");
        return nullptr;
    }
}

MediaContainer* SPIDriver::handle_text_batch(uint8_t* payload, size_t payload_length, uint8_t message_id) {
    if (payload_length < 5) {
        send_error(message_id, ErrorCode::INVALID_FORMAT, "Payload too short for Text Batch");
        return nullptr;
    }

    uint16_t bg_color = (payload[0] << 8) | payload[1];
    uint16_t font_color = (payload[2] << 8) | payload[3];
    uint8_t num_items = payload[4];
    size_t offset = 5;

    TextGroup* text_group = new TextGroup(0, bg_color, font_color);

    for (uint8_t i = 0; i < num_items; ++i) {
        if (offset + 8 > payload_length) {
            send_error(message_id, ErrorCode::INVALID_FORMAT, "Insufficient data for text item");
            delete text_group;
            return nullptr;
        }

        uint16_t x_pos = bytes_to_uint16(payload[offset], payload[offset + 1]);
        uint16_t y_pos = bytes_to_uint16(payload[offset + 2], payload[offset + 3]);
        FontID font_id = static_cast<FontID>(payload[offset + 4]);
        uint8_t text_length = payload[offset + 5];

        if (offset + 6 + text_length > payload_length) {
            send_error(message_id, ErrorCode::INVALID_FORMAT, "Text length exceeds payload");
            delete text_group;
            return nullptr;
        }

        String text = String((char*) &payload[offset + 6], text_length);
        Text* txt = new Text(text, 0, font_id, x_pos, y_pos);
        text_group->add_member(txt);

        offset += 6 + text_length;
    }

    send_ack(message_id, ErrorCode::SUCCESS);
    return text_group;
}

MediaContainer* SPIDriver::handle_image_transfer_start(uint8_t* payload, size_t payload_length, uint8_t message_id) {
    if (payload_length < 8) {
        send_error(message_id, ErrorCode::INVALID_FORMAT, "Payload too short for Image Transfer Start");
        return nullptr;
    }

    uint8_t image_id = payload[0];
    ImageFormat image_format = static_cast<ImageFormat>(payload[1]);
    uint32_t total_size = bytes_to_uint32(&payload[2]);
    uint16_t delay_time = bytes_to_uint16(payload[6], payload[7]);

    if (ongoing_transfers.find(image_id) != ongoing_transfers.end()) {
        send_error(message_id, ErrorCode::IMAGE_ID_MISMATCH, "Image ID already in use");
        return nullptr;
    }

    Image* image = new Image(image_id, image_format, total_size, delay_time, 0);

    if (image->get_status() == MediaStatus::EXPIRED) {
        send_error(message_id, ErrorCode::OUT_OF_MEMORY, "Failed to allocate memory for image");
        delete image;
        return nullptr;
    }

    ongoing_transfers[image_id] = image;
    send_ack(message_id, ErrorCode::SUCCESS);
    return nullptr;
}

MediaContainer* SPIDriver::handle_image_chunk(uint8_t* payload, size_t payload_length, uint8_t message_id) {
    if (payload_length < 3) {
        send_error(message_id, ErrorCode::INVALID_FORMAT, "Payload too short for Image Chunk");
        return nullptr;
    }

    uint8_t image_id = payload[0];
    uint16_t chunk_number = bytes_to_uint16(payload[1], payload[2]);
    size_t chunk_data_length = payload_length - 3;
    uint8_t* chunk_data = &payload[3];

    auto it = ongoing_transfers.find(image_id);
    if (it == ongoing_transfers.end()) {
        send_error(message_id, ErrorCode::IMAGE_ID_MISMATCH, "Image ID not found for chunk");
        return nullptr;
    }

    Image* image = static_cast<Image*>(it->second);
    image->add_chunk(chunk_number, chunk_data, chunk_data_length);

    send_ack(message_id, ErrorCode::SUCCESS);
    return nullptr;
}

MediaContainer* SPIDriver::handle_image_transfer_end(uint8_t* payload, size_t payload_length, uint8_t message_id) {
    if (payload_length != 1) {
        send_error(message_id, ErrorCode::INVALID_FORMAT, "Invalid payload length for Image Transfer End");
        return nullptr;
    }

    uint8_t image_id = payload[0];
    auto it = ongoing_transfers.find(image_id);
    if (it == ongoing_transfers.end()) {
        send_error(message_id, ErrorCode::IMAGE_ID_MISMATCH, "Image ID not found for transfer end");
        return nullptr;
    }

    Image* image = static_cast<Image*>(it->second);
    ongoing_transfers.erase(it);

    send_ack(message_id, ErrorCode::SUCCESS);
    return image;   // Return the completed image for display
}

MediaContainer* SPIDriver::handle_option_list(uint8_t* payload, size_t payload_length, uint8_t message_id) {
    if (payload_length < 2) {
        send_error(message_id, ErrorCode::INVALID_FORMAT, "Payload too short for Option List");
        return nullptr;
    }

    uint8_t selected_index = payload[0];
    uint8_t num_options = payload[1];
    size_t offset = 2;

    OptionGroup* option_group = new OptionGroup(selected_index);

    for (uint8_t i = 0; i < num_options; ++i) {
        if (offset >= payload_length) {
            send_error(message_id, ErrorCode::INVALID_FORMAT, "Insufficient data for option");
            delete option_group;
            return nullptr;
        }

        uint8_t option_length = payload[offset];
        if (offset + 1 + option_length > payload_length) {
            send_error(message_id, ErrorCode::INVALID_FORMAT, "Option length exceeds payload");
            delete option_group;
            return nullptr;
        }

        String option_text = String((char*) &payload[offset + 1], option_length);
        option_group->add_option(option_text);

        offset += 1 + option_length;
    }

    send_ack(message_id, ErrorCode::SUCCESS);
    return option_group;
}

MediaContainer* SPIDriver::handle_option_selection_update(uint8_t* payload, size_t payload_length, uint8_t message_id) {
    if (payload_length != 1) {
        send_error(message_id, ErrorCode::INVALID_FORMAT, "Invalid payload length for Option Selection Update");
        return nullptr;
    }

    uint8_t selected_index = payload[0];

    // Update the current option group if available
    // Assuming there's a current_option_group variable or method to handle this
    // current_option_group->set_selected_index(selected_index);

    send_ack(message_id, ErrorCode::SUCCESS);
    return nullptr;
}

MediaContainer* SPIDriver::handle_backlight_control(uint8_t* payload, size_t payload_length, uint8_t message_id) {
    if (payload_length != 1) {
        send_error(message_id, ErrorCode::INVALID_FORMAT, "Invalid payload length for Backlight Control");
        return nullptr;
    }

    uint8_t state = payload[0];

    // Control the backlight accordingly
    // Assuming there's a function to set backlight
    // set_backlight(state == 0x01);

    send_ack(message_id, ErrorCode::SUCCESS);
    return new MediaContainer(MediaType::BACKLIGHT_CONTROL, 0);
}
}   // namespace dice
