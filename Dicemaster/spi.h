#ifndef DICE_SPI
#define DICE_SPI

#include <vector>
#include <string>
#include <map>

#include <ESP32DMASPISlave.h>
#include "media.h"
#include "protocol.h"
#include "constants.h"

namespace dice {

// SPI Buffer Sizes
constexpr size_t SPI_MOSI_BUFFER_SIZE = 32768;  // Adjust as needed
constexpr size_t SPI_MISO_BUFFER_SIZE = 256;   // For ACK/NACK messages
constexpr size_t QUEUE_SIZE = 10;

class SPIDriver {
private:
    ESP32DMASPI::Slave slave;
    uint8_t* dma_tx_buf {nullptr};
    uint8_t* dma_rx_buf {nullptr};

    // Context Management
    std::map<uint8_t, MediaContainer*> ongoing_transfers;

    // ───────── Helpers to emit ACK / ERROR packets (using protocol.h) ────────
    void sendAck(uint8_t msgId, ErrorCode code = ErrorCode::SUCCESS)
    {
        Message ackMsg;
        ackMsg.hdr.marker = ::SOF_MARKER;
        ackMsg.hdr.type = MessageType::ACK;
        ackMsg.hdr.id = msgId;
        
        ackMsg.payload.tag = TAG_ACK;
        ackMsg.payload.u.ack.status = code;
        
        // Encode the ACK message
        size_t encodedSize = encode(dma_tx_buf, SPI_MISO_BUFFER_SIZE, ackMsg);
        if (encodedSize > 0) {
            slave.queue(dma_tx_buf, nullptr, encodedSize);
            Serial.println("[SPI] Sent ACK - ID: " + String(msgId) + ", Status: " + String(static_cast<uint8_t>(code)));
        } else {
            Serial.println("[SPI] Failed to encode ACK message");
        }
    }
    
    void sendError(uint8_t msgId, ErrorCode code, const char* txt)
    {
        Message errorMsg;
        errorMsg.hdr.marker = ::SOF_MARKER;
        errorMsg.hdr.type = MessageType::ERROR;
        errorMsg.hdr.id = msgId;
        
        errorMsg.payload.tag = TAG_ERROR;
        errorMsg.payload.u.error.code = code;
        errorMsg.payload.u.error.len = strlen(txt);
        strncpy(errorMsg.payload.u.error.text, txt, sizeof(errorMsg.payload.u.error.text) - 1);
        errorMsg.payload.u.error.text[sizeof(errorMsg.payload.u.error.text) - 1] = '\0';
        
        // Encode the ERROR message
        size_t encodedSize = encode(dma_tx_buf, SPI_MISO_BUFFER_SIZE, errorMsg);
        if (encodedSize > 0) {
            slave.queue(dma_tx_buf, nullptr, encodedSize);
            Serial.println("[SPI] Sent ERROR - ID: " + String(msgId) + ", Code: " + String(static_cast<uint8_t>(code)));
            Serial.println("[SPI] Error Message: " + String(txt));
        } else {
            Serial.println("[SPI] Failed to encode ERROR message");
        }
    }

    // ───────── Message‑type specific handlers (return MediaContainer or NULL)
    MediaContainer* handle(const DProtocol::TextBatch&   tb);
    MediaContainer* handle(const DProtocol::ImageStart&  is);
    MediaContainer* handle(const DProtocol::ImageChunk&  ic);
    MediaContainer* handle(const DProtocol::ImageEnd&    ie);
    MediaContainer* handle(const DProtocol::OptionList&  ol);
    MediaContainer* handle(const DProtocol::OptionSelectionUpdate& os);

public:
    SPIDriver();
    
    /** Legacy API compatibility - now calls queueTransaction */
    // void queue_cmd_msgs() { queueTransaction(); }
    
    /** Legacy API compatibility - now calls poll */
    // std::vector<MediaContainer*> process_msgs() { return poll(); }
    
    /** queue a new background DMA transaction if no outstanding one */
    void queueTransaction();

    /** Poll the SPI slave for completed transactions – return zero‑or‑more
        ready MediaContainers that the application can now display. */
    std::vector<MediaContainer*> poll();
    
    /** Debug mode: Poll SPI and return hex representation of received bytes */
    MediaContainer* pollDebugHex();
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
        Serial.println("Handling!!!");
        slave.queue(NULL, dma_rx_buf, SPI_MOSI_BUFFER_SIZE);
        slave.trigger();
    }
}


// ───────────────────────── queue-/poll helpers ────────────────────────
inline std::vector<MediaContainer*> SPIDriver::poll()
{
    std::vector<MediaContainer*> ready;

    // if (!slave.hasTransactionsCompletedAndAllResultsReady(QUEUE_SIZE))
    //     return ready;

    if (slave.numTransactionsCompleted() == 0) {
        // Serial.println("[SPI POLL] No completed transactions to process");
        return ready;
    }

    const std::vector<size_t> sizes = slave.numBytesReceivedAll();
    size_t offset = 0;
    Serial.println("[SPI POLL] Received " + String(sizes.size()) + " transactions");
    for (size_t sz : sizes)
    {
        const uint8_t* buf = dma_rx_buf + offset;
        offset += sz;

        // Debug: Print detailed message info before parsing
        Serial.println("[SPI PARSE] Processing message of " + String(sz) + " bytes");
        Serial.print("[SPI PARSE] First 16 bytes: ");
        for (size_t i = 0; i < min(sz, (size_t)16); i++) {
            if (buf[i] < 0x10) Serial.print("0");
            Serial.print(buf[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
        
        // Check SOF marker
        if (sz > 0) {
            Serial.println("[SPI PARSE] SOF marker: 0x" + String(buf[0], HEX) + " (expected: 0x7E)");
        }
        if (sz > 1) {
            Serial.println("[SPI PARSE] Message type: 0x" + String(buf[1], HEX));
        }
        if (sz > 2) {
            Serial.println("[SPI PARSE] Message ID: 0x" + String(buf[2], HEX));
        }

        DProtocol::Message msg;
        ErrorCode ec = DProtocol::decode(buf, sz, msg);

        if (ec != ErrorCode::SUCCESS)            // parsing failed
        {
            Serial.println("[SPI ERROR] Decode failed with error code: " + String(static_cast<uint8_t>(ec)));
            
            // Provide detailed error descriptions
            String error_desc = "UNKNOWN_ERROR";
            switch(ec) {
                // Header errors
                case ErrorCode::HEADER_TOO_SHORT: error_desc = "HEADER_TOO_SHORT"; break;
                case ErrorCode::INVALID_SOF_MARKER: error_desc = "INVALID_SOF_MARKER"; break;
                case ErrorCode::INVALID_MESSAGE_TYPE: error_desc = "INVALID_MESSAGE_TYPE"; break;
                case ErrorCode::INVALID_LENGTH_FIELD: error_desc = "INVALID_LENGTH_FIELD"; break;
                case ErrorCode::HEADER_LENGTH_MISMATCH: error_desc = "HEADER_LENGTH_MISMATCH"; break;
                
                // TextBatch errors
                case ErrorCode::TEXT_PAYLOAD_TOO_SHORT: error_desc = "TEXT_PAYLOAD_TOO_SHORT"; break;
                case ErrorCode::TEXT_TOO_MANY_ITEMS: error_desc = "TEXT_TOO_MANY_ITEMS"; break;
                case ErrorCode::TEXT_INVALID_ROTATION: error_desc = "TEXT_INVALID_ROTATION"; break;
                case ErrorCode::TEXT_ITEM_HEADER_TOO_SHORT: error_desc = "TEXT_ITEM_HEADER_TOO_SHORT"; break;
                case ErrorCode::TEXT_ITEM_LENGTH_MISMATCH: error_desc = "TEXT_ITEM_LENGTH_MISMATCH"; break;
                case ErrorCode::TEXT_PAYLOAD_TRUNCATED: error_desc = "TEXT_PAYLOAD_TRUNCATED"; break;
                case ErrorCode::TEXT_LENGTH_CALCULATION_ERROR: error_desc = "TEXT_LENGTH_CALCULATION_ERROR"; break;
                
                // Image errors
                case ErrorCode::IMAGE_START_TOO_SHORT: error_desc = "IMAGE_START_TOO_SHORT"; break;
                case ErrorCode::IMAGE_START_INVALID_ROTATION: error_desc = "IMAGE_START_INVALID_ROTATION"; break;
                case ErrorCode::IMAGE_START_INVALID_FORMAT: error_desc = "IMAGE_START_INVALID_FORMAT"; break;
                case ErrorCode::IMAGE_START_INVALID_RESOLUTION: error_desc = "IMAGE_START_INVALID_RESOLUTION"; break;
                case ErrorCode::IMAGE_CHUNK_TOO_SHORT: error_desc = "IMAGE_CHUNK_TOO_SHORT"; break;
                case ErrorCode::IMAGE_CHUNK_DATA_TRUNCATED: error_desc = "IMAGE_CHUNK_DATA_TRUNCATED"; break;
                case ErrorCode::IMAGE_CHUNK_INVALID_LENGTH: error_desc = "IMAGE_CHUNK_INVALID_LENGTH"; break;
                case ErrorCode::IMAGE_END_TOO_SHORT: error_desc = "IMAGE_END_TOO_SHORT"; break;
                
                // Legacy errors
                case ErrorCode::INVALID_FORMAT: error_desc = "INVALID_FORMAT"; break;
                case ErrorCode::UNKNOWN_MSG_TYPE: error_desc = "UNKNOWN_MSG_TYPE"; break;
                case ErrorCode::PAYLOAD_LENGTH_MISMATCH: error_desc = "PAYLOAD_LENGTH_MISMATCH"; break;
                case ErrorCode::UNSUPPORTED_MESSAGE: error_desc = "UNSUPPORTED_MESSAGE"; break;
                
                default: error_desc = "ERROR_CODE_" + String(static_cast<uint8_t>(ec)); break;
            }
            
            Serial.println("[SPI ERROR] Error meaning: " + error_desc);
            sendError(sz > 2 ? buf[2] : 0 /*msgId*/, ec, "decode");
            continue;
        }

        Serial.println("[SPI PARSE] Successfully decoded message - Type: " + String(static_cast<uint8_t>(msg.hdr.type)) + ", ID: " + String(msg.hdr.id));

        uint8_t id = msg.hdr.id;
        MediaContainer* out = nullptr;

        Serial.println("[SPI PARSE] Processing payload tag: " + String(static_cast<uint8_t>(msg.payload.tag)));

        switch (msg.payload.tag)
        {
            case DProtocol::TAG_TEXT_BATCH:
                Serial.println("[SPI PARSE] Handling TEXT_BATCH message");
                out = handle(msg.payload.u.textBatch);
                break;

            case DProtocol::TAG_IMAGE_START:
                Serial.println("[SPI PARSE] Handling IMAGE_START message");
                out = handle(msg.payload.u.imageStart);
                break;

            case DProtocol::TAG_IMAGE_CHUNK:
                Serial.println("[SPI PARSE] Handling IMAGE_CHUNK message");
                out = handle(msg.payload.u.imageChunk);
                break;

            case DProtocol::TAG_IMAGE_END:
                Serial.println("[SPI PARSE] Handling IMAGE_END message");
                out = handle(msg.payload.u.imageEnd);
                break;

            case DProtocol::TAG_OPTION_LIST:
                Serial.println("[SPI PARSE] Handling OPTION_LIST message");
                out = handle(msg.payload.u.optionList);
                break;

            case DProtocol::TAG_OPTION_UPDATE:
                out = handle(msg.payload.u.optionUpdate);
                break;

            case DProtocol::TAG_BACKLIGHT_ON:
                /* turn back-light on */;
                break;

            case DProtocol::TAG_BACKLIGHT_OFF:
                /* turn back-light off */;
                break;

            case DProtocol::TAG_ACK:     /* host ACK – ignore */  break;
            case DProtocol::TAG_ERROR:   /* host error – log  */  break;

            default:
                sendError(id, ErrorCode::UNKNOWN_MSG_TYPE, "tag?");
                break;
        }

        if (out) ready.push_back(out);
        // sendAck(id);                               // ACK disabled - not waiting for ACKs
    }
    return ready;
}

// ─────────────────────────── TextBatch handler ────────────────────────
inline MediaContainer* SPIDriver::handle(const DProtocol::TextBatch& tb)
{
    Serial.println("[SPI TEXT] Processing TextBatch with " + String(tb.itemCount) + " items");
    Serial.println("[SPI TEXT] BG Color: 0x" + String(tb.bgColor, HEX) + ", Rotation: " + String(tb.rotation));
    
    // Create a TextGroup with the background color from TextBatch
    // The font color from TextBatch is now ignored since each Text has its own color
    auto* group = new TextGroup(0, tb.bgColor, 0xFFFF);  // Default white for group font color

    for (uint8_t i = 0; i < tb.itemCount; ++i)
    {
        const DProtocol::TextItem& it = tb.items[i];
        
        Serial.println("[SPI TEXT] Item " + String(i) + ": len=" + String(it.len) + 
                      ", font=" + String(it.font) + ", color=0x" + String(it.color, HEX) + 
                      ", pos=(" + String(it.x) + "," + String(it.y) + ")");
        
        // Print the text content for debugging
        String text_content = String(reinterpret_cast<const char*>(it.text), it.len);
        Serial.println("[SPI TEXT] Text content: '" + text_content + "'");
        
        // Create Text with individual color
        Text* t = new Text(
            text_content,
            0,
            static_cast<FontID>(it.font),
            it.x,
            it.y,
            it.color  // Use individual color from TextItem
        );
        group->add_member(t);
    }
    return group;
}

// ─────────────────────────── ImageStart handler ───────────────────────
inline MediaContainer* SPIDriver::handle(const DProtocol::ImageStart& is)
{
    if (ongoing_transfers.count(is.imgId))
    {
        sendError(/*msgId=*/0, ErrorCode::IMAGE_ID_MISMATCH, "img id busy");
        return nullptr;
    }

    ImageFormat     fmt = static_cast<ImageFormat>(is.fmtRes >> 4);
    ImageResolution res = static_cast<ImageResolution>(is.fmtRes & 0x0F);

    Image* img = new Image(is.imgId, fmt, res, is.totalSize, is.delayMs);
    ongoing_transfers[is.imgId] = img;    // wait for chunks
    return nullptr;
}

// ─────────────────────────── ImageChunk handler ───────────────────────
inline MediaContainer* SPIDriver::handle(const DProtocol::ImageChunk& ic)
{
    auto it = ongoing_transfers.find(ic.imgId);
    if (it == ongoing_transfers.end())
    {
        sendError(/*msgId=*/0, ErrorCode::IMAGE_ID_MISMATCH, "chunk w/o start");
        return nullptr;
    }

    static_cast<Image*>(it->second)->add_chunk(ic.data, ic.length);
    return nullptr;
}

// ImageEnd & option handlers you posted earlier remain valid

inline MediaContainer* SPIDriver::handle(const DProtocol::ImageEnd& ie)
{
    auto it = ongoing_transfers.find(ie.imgId);
    if (it == ongoing_transfers.end()) {
        sendError(/*msgId=*/0, ErrorCode::IMAGE_ID_MISMATCH, "end w/o start");
        return nullptr;
    }
    MediaContainer* complete = it->second;
    ongoing_transfers.erase(it);
    return complete;         // return ready‑to‑display image
}

inline MediaContainer* SPIDriver::handle(const DProtocol::OptionList& ol)
{
    // TODO: convert to your UI Option container; placeholder logs only
    (void)ol; return nullptr;
}

inline MediaContainer* SPIDriver::handle(const DProtocol::OptionSelectionUpdate& os)
{
    // TODO: apply selection; placeholder
    (void)os; return nullptr;
}

// ─────────────────────────── Debug Mode Implementation ────────────────────────
inline MediaContainer* SPIDriver::pollDebugHex()
{
    static int message_counter = 0;  // Track total messages received
    
    if (slave.numTransactionsCompleted() == 0) {
        // Serial.println("[SPI DEBUG] No completed transactions to process");
        return nullptr;
    }

    const std::vector<size_t> sizes = slave.numBytesReceivedAll();
    if (sizes.empty() || sizes[0] == 0) 
        return nullptr;

    size_t total_bytes = sizes[0];
    message_counter++;
    
    // Print message length to serial first
    Serial.println("[SPI DEBUG] Message #" + String(message_counter) + " received - Length: " + String(total_bytes) + " bytes");
    
    String hex_output = "SPI RX (" + String(total_bytes) + " bytes):\n";
    
    // Convert received bytes to hex string with better formatting
    for (size_t i = 0; i < total_bytes && i < SPI_MOSI_BUFFER_SIZE; i++) {
        if (i > 0 && i % 16 == 0) {
            hex_output += "\n";  // New line every 16 bytes
        } else if (i > 0 && i % 8 == 0) {
            hex_output += "  ";   // Extra space every 8 bytes
        } else if (i > 0) {
            hex_output += " ";    // Space between bytes
        }
        
        uint8_t byte_val = dma_rx_buf[i];
        if (byte_val < 0x10) {
            hex_output += "0";   // Add leading zero for single digit hex
        }
        hex_output += String(byte_val, HEX);
    }
    
    // Add ASCII representation for better debugging
    hex_output += "\n\nASCII: ";
    for (size_t i = 0; i < total_bytes && i < SPI_MOSI_BUFFER_SIZE; i++) {
        uint8_t byte_val = dma_rx_buf[i];
        if (byte_val >= 32 && byte_val <= 126) {
            hex_output += char(byte_val);  // Printable ASCII
        } else {
            hex_output += ".";  // Non-printable character
        }
    }
    
    // Also print to serial for debugging
    Serial.println("[SPI DEBUG] " + hex_output);
    
    // Create text display showing the hex data
    auto* text_group = new TextGroup(0, DICE_BLACK, DICE_GREEN);
    Text* hex_text = new Text(hex_output, 3000, FontID::TF, 10, 30);  // Display for 3 seconds
    text_group->add_member(hex_text);
    
    return text_group;
}

} // namespace dice
#endif
