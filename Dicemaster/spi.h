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

class SPIDriver {
private:
    ESP32DMASPI::Slave slave;
    uint8_t* dma_tx_buf {nullptr};
    uint8_t* dma_rx_buf {nullptr};

    // Context Management
    // uint8_t expecting_image_id;
    // MediaContainer* expecting_container;
    std::map<uint8_t, MediaContainer*> ongoing_transfers;

    // ───────── Helpers to emit ACK / ERROR packets (using protocol.h) ────────
    void sendAck(uint8_t msgId, ErrorCode code = ErrorCode::SUCCESS)
    {
        Protocol::Ack ack{code};
        auto pkt = Protocol::encode(ack, msgId);   // returns std::vector<uint8_t>
        slave.queue(pkt.data(), nullptr, pkt.size());
    }
    void sendError(uint8_t msgId, ErrorCode code, const char* txt)
    {
        Protocol::Error err{code, txt};
        auto pkt = Protocol::encode(err, msgId);
        slave.queue(pkt.data(), nullptr, pkt.size());
    }

    // ───────── Message‑type specific handlers (return MediaContainer or NULL)
    MediaContainer* handle(const Protocol::TextBatch&   tb);
    MediaContainer* handle(const Protocol::ImageStart&  is);
    MediaContainer* handle(const Protocol::ImageChunk&  ic);
    MediaContainer* handle(const Protocol::ImageEnd&    ie);
    MediaContainer* handle(const Protocol::OptionList&  ol);
    MediaContainer* handle(const Protocol::OptionSelectionUpdate& os);

public:
    SPIDriver();
    /** queue a new background DMA transaction if no outstanding one */
    void queueTransaction();

    /** Poll the SPI slave for completed transactions – return zero‑or‑more
        ready MediaContainers that the application can now display. */
    std::vector<MediaContainer*> poll();
};


inline SPIDriver::SPIDriver() {
    // Initialize SPI Slave
    dma_tx_buf = slave.allocDMABuffer(SPI_MISO_BUFFER_SIZE);
    dma_rx_buf = slave.allocDMABuffer(SPI_MOSI_BUFFER_SIZE);

    slave.setDataMode(SPI_MODE0);
    slave.setMaxTransferSize(SPI_MOSI_BUFFER_SIZE);
    slave.setQueueSize(QUEUE_SIZE);
    slave.begin();   // Default HSPI
}

inline void SPIDriver::queueTransaction()
{
    if (slave.hasTransactionsCompletedAndAllResultsHandled()) {
        slave.queue(dma_tx_buf, dma_rx_buf, SPI_MOSI_BUFFER_SIZE);
        slave.trigger();
    }
}

inline std::vector<MediaContainer*> SPIDriver::poll()
{
    std::vector<MediaContainer*> ready;

    if (!slave.hasTransactionsCompletedAndAllResultsReady(QUEUE_SIZE))
        return ready;                               // nothing yet

    const auto sizes = slave.numBytesReceivedAll();
    size_t offset = 0;

    for (size_t sz : sizes) {
        const uint8_t* buf = dma_rx_buf + offset;
        offset += sz;
        try {
            auto msg = Protocol::decodeMessage(buf, sz);
            uint8_t id = msg.header.id;

            // ─── Dispatch via std::visit on variant payload ────────────────
            std::visit([&](auto&& payload){
                using T = std::decay_t<decltype(payload)>;
                MediaContainer* out = nullptr;
                if constexpr (std::is_same_v<T, Protocol::TextBatch>)              out = handle(payload);
                else if constexpr (std::is_same_v<T, Protocol::ImageStart>)        out = handle(payload);
                else if constexpr (std::is_same_v<T, Protocol::ImageChunk>)        out = handle(payload);
                else if constexpr (std::is_same_v<T, Protocol::ImageEnd>)          out = handle(payload);
                else if constexpr (std::is_same_v<T, Protocol::OptionList>)        out = handle(payload);
                else if constexpr (std::is_same_v<T, Protocol::OptionSelectionUpdate>) out = handle(payload);
                // Back‑light & ACK/ERROR are handled immediately (no container)
                else if constexpr (std::is_same_v<T, Protocol::BacklightOn>)  /*turn on*/;
                else if constexpr (std::is_same_v<T, Protocol::BacklightOff>) /*turn off*/;
                else if constexpr (std::is_same_v<T, Protocol::Ack>)               /*ignore*/;
                else if constexpr (std::is_same_v<T, Protocol::Error>)             /*log*/;

                if (out) ready.push_back(out);
                sendAck(id);   // generic success ACK
            }, msg.payload);
        }
        catch(const std::exception& e){
            sendError(buf[2] /*msgId*/, ErrorCode::INVALID_FORMAT, e.what());
        }
    }
    return ready;
}



// ──────────────────────────── Per‑type handlers ─────────────────────────────

inline MediaContainer* SPIDriver::handle(const Protocol::TextBatch& tb)
{
    auto* group = new TextGroup(0, tb.bg_color, tb.font_color);
    for (const auto& it : tb.items) {
        Text* t = new Text(String((const char*)it.text.data(), it.text.size()),
                           0, static_cast<FontID>(it.font_id), it.x, it.y);
        group->add_member(t);
    }
    return group;
}

inline MediaContainer* SPIDriver::handle(const Protocol::ImageStart& is)
{
    if (ongoing.count(is.img_id)) {
        sendError(/*msgId=*/0, ErrorCode::IMAGE_ID_MISMATCH, "img id busy");
        return nullptr;
    }
    Image* img = new Image(is.img_id, is.fmt, is.res, is.total_size, is.delay_ms);
    ongoing[is.img_id] = img;   // wait for chunks
    return nullptr;
}

inline MediaContainer* SPIDriver::handle(const Protocol::ImageChunk& ic)
{
    auto it = ongoing.find(ic.img_id);
    if (it == ongoing.end()) {
        sendError(/*msgId=*/0, ErrorCode::IMAGE_ID_MISMATCH, "chunk w/o start");
        return nullptr;
    }
    static_cast<Image*>(it->second)->add_chunk(ic.data.data(), ic.data.size());
    return nullptr;
}

inline MediaContainer* SPIDriver::handle(const Protocol::ImageEnd& ie)
{
    auto it = ongoing.find(ie.img_id);
    if (it == ongoing.end()) {
        sendError(/*msgId=*/0, ErrorCode::IMAGE_ID_MISMATCH, "end w/o start");
        return nullptr;
    }
    MediaContainer* complete = it->second;
    ongoing.erase(it);
    return complete;         // return ready‑to‑display image
}

inline MediaContainer* SPIDriver::handle(const Protocol::OptionList& ol)
{
    // TODO: convert to your UI Option container; placeholder logs only
    (void)ol; return nullptr;
}

inline MediaContainer* SPIDriver::handle(const Protocol::OptionSelectionUpdate& os)
{
    // TODO: apply selection; placeholder
    (void)os; return nullptr;
}

// MediaContainer* SPIDriver::parse_message(uint8_t* buf, size_t buf_size) {
//     if (buf[0] != SOF_MARKER) {
//         // Invalid Start of Frame
//         return print_error("Invalid SOF Received");
//     }
//     MessageType message_type = static_cast<MessageType>(buf[1]);
//     uint8_t message_id = buf[2];
//     uint16_t payload_length = (buf[3] << 8) | buf[4];
//     uint8_t* payload = buf+5;

//     switch (message_type) {
//         case MessageType::TEXT_BATCH:
//           return handle_text_batch(payload, payload_length, message_id);
//         case MessageType::IMAGE_TRANSFER_START:
//             Serial.println("Transfer Start");
//             return handle_image_transfer_start(payload, payload_length, message_id);
//         case MessageType::IMAGE_CHUNK:
//             Serial.println("Parse Chunk");
//             return handle_image_chunk(payload, payload_length, message_id);
//         case MessageType::IMAGE_TRANSFER_END:
//             Serial.println("Parse End");
//             return handle_image_transfer_end(payload, payload_length, message_id);
//         // case MessageType::OPTION_LIST:
//         //     return handle_option_list(payload, payload_length, message_id);
//         // case MessageType::OPTION_SELECTION_UPDATE:
//         //     return handle_option_selection_update(payload, payload_length, message_id);
//         // case MessageType::BACKLIGHT_CONTROL:
//         //     return handle_backlight_control(payload, payload_length, message_id);
//         default:
//           // Unknown Message Type
//           send_error(message_id, ErrorCode::UNKNOWN_MSG_TYPE, "Unknown Message Type");
//           return print_error("Unknown Message Type!");
//     }
// }

// MediaContainer* SPIDriver::handle_text_batch(uint8_t* payload, size_t payload_length, uint8_t message_id) {
//     // Specification at
//     // https://docs.google.com/document/d/1ovbKFz1-aYnTLMupWtqQHsDRdrbPbAs7edm_ehnVuko/
//     if (payload_length < 5) {
//         return print_error("String Length Less than 5!");
//     }

//     uint16_t bg_color = (payload[0] << 8) | payload[1];
//     uint16_t font_color = (payload[2] << 8) | payload[3];
//     uint8_t num_items = payload[4];
//     size_t offset = 5;

//     TextGroup* text_group = new TextGroup(0, bg_color, font_color);

//     for (uint8_t i = 0; i < num_items; ++i) {
//         if (offset + 6 > payload_length) {
//             delete text_group;
//             return print_error("Not Enough Content for Text!");
//         }

//         uint16_t x_pos = bytes_to_uint16(payload[offset], payload[offset + 1]);
//         uint16_t y_pos = bytes_to_uint16(payload[offset + 2], payload[offset + 3]);
//         FontID font_id = static_cast<FontID>(payload[offset + 4]);
//         uint8_t text_length = payload[offset + 5];

//         if (offset + 6 + text_length > payload_length) {
//             delete text_group;
//             return print_error("Text length exceeds payload!");
//         }

//         String text = String((char*) &payload[offset + 6], text_length);
//         Text* txt = new Text(text, 0, font_id, x_pos, y_pos);
//         text_group->add_member(txt);

//         offset += 6 + text_length;
//     }

//     send_ack(message_id, ErrorCode::SUCCESS);
//     return text_group;
// }

// MediaContainer* SPIDriver::handle_image_transfer_start(uint8_t* payload, size_t payload_length, uint8_t message_id) {
//     // Specification at
//     // https://docs.google.com/document/d/1ovbKFz1-aYnTLMupWtqQHsDRdrbPbAs7edm_ehnVuko/
//     if (payload_length < 9) {
//         send_error(message_id, ErrorCode::INVALID_FORMAT, "Payload too short for Image Transfer Start");
//         Serial.println("Invalid Format in Start!");
//         return nullptr;
//     }

//     uint8_t image_id = payload[0];
//     ImageFormat image_format = static_cast<ImageFormat>(payload[1]);
//     ImageResolution image_resolution = static_cast<ImageResolution>(payload[2]);
//     uint32_t total_size = bytes_to_uint32(&payload[3]);
//     uint16_t delay_time = bytes_to_uint16(payload[6], payload[7]);

//     if (ongoing_transfers.find(image_id) != ongoing_transfers.end()) {
//         send_error(message_id, ErrorCode::IMAGE_ID_MISMATCH, "Image ID already in use");
//         Serial.println("Image ID already in use!");
//         return nullptr;
//     }

//     Image* image = new Image(image_id, image_format, image_resolution, total_size, 0);

//     if (image->get_status() == MediaStatus::EXPIRED) {
//         Serial.println("Media Expired!");
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
//         Serial.println("Image ID Not Found!");
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
//         Serial.println("Invalid Payload Length for img end!");
//         return nullptr;
//     }

//     uint8_t image_id = payload[0];
//     auto it = ongoing_transfers.find(image_id);
//     if (it == ongoing_transfers.end()) {
//         send_error(message_id, ErrorCode::IMAGE_ID_MISMATCH, "Image ID not found for transfer end");
//         Serial.println("Image ID not found!");
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
