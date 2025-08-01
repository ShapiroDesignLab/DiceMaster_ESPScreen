#ifndef DICE_SPI
#define DICE_SPI

#include <vector>
#include <string>
#include <map>

#include <ESP32DMASPISlave.h>
#include "media.h"
#include "protocol.h"
#include "constants.h"
#include "decoding_handler.h"

namespace dice {

// SPI Buffer Sizes
constexpr size_t SPI_MOSI_BUFFER_SIZE = 8192;  // Increased for larger messages
constexpr size_t QUEUE_SIZE = 1;                // Single transaction at a time for fastest turnaround 

class SPIDriver {
private:
    ESP32DMASPI::Slave slave;
    uint8_t* dma_tx_buf {nullptr};
    uint8_t* dma_rx_buf {nullptr};

    // Decoding handler for asynchronous processing
    DecodingHandler* decoding_handler;

    // Callback counters for debugging
    volatile size_t transaction_count = 0;

    // User-defined callback for SPI transaction completion
    static void IRAM_ATTR spi_transaction_callback(spi_slave_transaction_t *trans, void *arg);

public:
    SPIDriver();
    ~SPIDriver();
    
    /** Initialize SPI and start first transaction */
    bool initialize();
    
    /** Get decoded media containers from the decoding handler (called at 30Hz) */
    std::vector<MediaContainer*> get_decoded_media();
    
    /** Get decoding handler statistics */
    DecodingHandler::Statistics get_decode_statistics() const;
    
    /** Get SPI transaction count */
    size_t get_transaction_count() const { return transaction_count; }
;

// Implementation
inline SPIDriver::SPIDriver() : decoding_handler(nullptr) {
    // Initialize SPI Slave with single buffer for fastest turnaround
    dma_rx_buf = slave.allocDMABuffer(SPI_MOSI_BUFFER_SIZE);
    dma_tx_buf = nullptr; // No TX buffer needed since we're not replying

    slave.setDataMode(SPI_MODE0);
    slave.setMaxTransferSize(SPI_MOSI_BUFFER_SIZE);
    slave.setQueueSize(QUEUE_SIZE);
    slave.begin();   // Default HSPI
    
    // Initialize decoding handler
    decoding_handler = new DecodingHandler();
}

inline SPIDriver::~SPIDriver() {
    if (decoding_handler) {
        decoding_handler->shutdown();
        delete decoding_handler;
        decoding_handler = nullptr;
    }
}

inline bool SPIDriver::initialize() {
    if (!decoding_handler || !decoding_handler->initialize()) {
        Serial.println("[SPI] Failed to initialize decoding handler");
        return false;
    }
    
    // Set up SPI callback
    slave.setUserPostTransCbAndArg(spi_transaction_callback, (void*)this);
    
    // Start first transaction
    slave.queue(nullptr, dma_rx_buf, SPI_MOSI_BUFFER_SIZE);
    slave.trigger();
    
    Serial.println("[SPI] Initialized with callback-based processing");
    return true;
}

// Static callback function - called from ISR context
inline void IRAM_ATTR SPIDriver::spi_transaction_callback(spi_slave_transaction_t *trans, void *arg) {
    // NOTE: This runs in ISR context - be very careful what we do here!
    SPIDriver* spi_driver = static_cast<SPIDriver*>(arg);
    
    // Increment transaction counter
    spi_driver->transaction_count++;
    
    // Get the received data size
    size_t received_bytes = trans->length / 8; // Convert bits to bytes
    
    if (received_bytes > 0 && spi_driver->decoding_handler) {
        // Copy data to PSRAM and enqueue for decoding (non-blocking)
        spi_driver->decoding_handler->enqueue_raw_data(spi_driver->dma_rx_buf, received_bytes);
    }
    
    // Immediately queue next transaction for fastest turnaround
    // This must be done in ISR for minimum latency
    if (spi_driver->slave.hasTransactionsCompletedAndAllResultsHandled()) {
        spi_driver->slave.queue(nullptr, spi_driver->dma_rx_buf, SPI_MOSI_BUFFER_SIZE);
        spi_driver->slave.trigger();
    }
}

inline std::vector<MediaContainer*> SPIDriver::get_decoded_media() {
    if (!decoding_handler) {
        return std::vector<MediaContainer*>();
    }
    return decoding_handler->get_decoded_media();
}

inline DecodingHandler::Statistics SPIDriver::get_decode_statistics() const {
    if (!decoding_handler) {
        return DecodingHandler::Statistics{};
    }
    return decoding_handler->get_statistics();
}
    }

    Serial.println("[SPI FAST] Received " + String(sizes.size()) + " transactions");

    // Process each completed transaction
    size_t offset = 0;
    for (size_t sz : sizes) {
        const uint8_t* buf = dma_rx_buf + offset;
        offset += sz;

        if (sz == 0) continue;

        Serial.println("[SPI FAST] Processing " + String(sz) + " bytes");

        // If task handler is available, use fast path
        if (task_handler) {
            // Fast capture: copy to PSRAM and immediately re-queue SPI
            if (task_handler->capture_spi_data(buf, sz)) {
                Serial.println("[SPI FAST] Data captured to PSRAM for async processing");
            } else {
                Serial.println("[SPI FAST] Failed to capture data - falling back to sync");
                // Fall back to synchronous processing for this message
                std::vector<MediaContainer*> sync_result = process_sync_message(buf, sz);
                ready.insert(ready.end(), sync_result.begin(), sync_result.end());
            }
        } else {
            // No task handler - process synchronously
            std::vector<MediaContainer*> sync_result = process_sync_message(buf, sz);
            ready.insert(ready.end(), sync_result.begin(), sync_result.end());
        }
    }

    // Handle any pending replies from the task handler
    if (task_handler) {
        std::vector<SPITaskHandler::SPIReplyData> replies = task_handler->get_pending_replies();
        for (const auto& reply_data : replies) {
            if (reply_data.hasReply) {
                reply(reply_data.msgId, reply_data.errorCode);
            }
        }
    }

    // Immediately re-queue SPI transaction for next message
    queueTransaction();

    return ready;
}

// Helper method for synchronous message processing
inline std::vector<MediaContainer*> SPIDriver::process_sync_message(const uint8_t* buf, size_t sz) {
    std::vector<MediaContainer*> ready;
    
    // Extract message ID for reply
    uint8_t msgId = (sz > 2) ? buf[2] : 0;
    
    DProtocol::Message msg;
    ErrorCode ec = DProtocol::decode(buf, sz, msg);
    
    if (ec != ErrorCode::SUCCESS) {
        Serial.println("[SPI SYNC] Decode failed: " + String(static_cast<uint8_t>(ec)));
        reply(msgId, static_cast<uint32_t>(ec));
        return ready;
    }
    
    MediaContainer* out = nullptr;
    
    switch (msg.payload.tag) {
        case DProtocol::TAG_TEXT_BATCH:
            out = handle(msg.payload.u.textBatch);
            break;
        case DProtocol::TAG_IMAGE_START:
            out = handle(msg.payload.u.imageStart);
            break;
        case DProtocol::TAG_IMAGE_CHUNK:
            out = handle(msg.payload.u.imageChunk);
            break;
        case DProtocol::TAG_IMAGE_END:
            out = handle(msg.payload.u.imageEnd);
            break;
        default:
            reply(msgId, static_cast<uint32_t>(ErrorCode::UNKNOWN_MSG_TYPE));
            return ready;
    }
    
    if (out) ready.push_back(out);
    reply(msgId, 0); // Success
    
    return ready;
}
            continue;
        }

        Serial.println("[SPI PARSE] Successfully decoded message - Type: " + String(static_cast<uint8_t>(msg.hdr.type)) + ", ID: " + String(msg.hdr.id));

        uint8_t id = msg.hdr.id;
        lastMsgId = id;
        lastErrorCode = ErrorCode::SUCCESS;
        hasValidMessage = true;
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

            case DProtocol::TAG_BACKLIGHT_ON:
                /* turn back-light on */;
                break;

            case DProtocol::TAG_BACKLIGHT_OFF:
                /* turn back-light off */;
                break;

            case DProtocol::TAG_ACK:     /* host ACK – ignore */  break;
            case DProtocol::TAG_ERROR:   /* host error – log  */  break;

            default:
                reply(id, static_cast<uint32_t>(ErrorCode::UNKNOWN_MSG_TYPE));
                break;
        }

        if (out) ready.push_back(out);
    }

    // Send reply for the last processed message
    if (hasValidMessage) {
        reply(lastMsgId, static_cast<uint32_t>(lastErrorCode));
    }

    return ready;
}

// ───────────────────────── Legacy Synchronous Poll (for debugging) ────────────────────────
inline std::vector<MediaContainer*> SPIDriver::poll_synchronous()
{
    std::vector<MediaContainer*> ready;

    if (slave.numTransactionsCompleted() == 0) {
        return ready;
    }

    const std::vector<size_t> sizes = slave.numBytesReceivedAll();
    size_t offset = 0;
    Serial.println("[SPI SYNC POLL] Received " + String(sizes.size()) + " transactions");

    // Track the last valid message for reply
    uint8_t lastMsgId = 0;
    ErrorCode lastErrorCode = ErrorCode::SUCCESS;
    bool hasValidMessage = false;

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
            reply(sz > 2 ? buf[2] : 0 /*msgId*/, static_cast<uint32_t>(ec));
            lastMsgId = sz > 2 ? buf[2] : 0;
            lastErrorCode = ec;
            hasValidMessage = true;

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
        reply(/*msgId=*/0, static_cast<uint32_t>(ErrorCode::IMAGE_ID_MISMATCH));
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
        reply(/*msgId=*/0, static_cast<uint32_t>(ErrorCode::IMAGE_ID_MISMATCH));
        return nullptr;
    }

    static_cast<Image*>(it->second)->add_chunk(ic.data, ic.length);
    return nullptr;
}

// ImageEnd handlers you posted earlier remain valid

inline MediaContainer* SPIDriver::handle(const DProtocol::ImageEnd& ie)
{
    auto it = ongoing_transfers.find(ie.imgId);
    if (it == ongoing_transfers.end()) {
        reply(/*msgId=*/0, static_cast<uint32_t>(ErrorCode::IMAGE_ID_MISMATCH));
        return nullptr;
    }
    MediaContainer* complete = it->second;
    ongoing_transfers.erase(it);
    return complete;         // return ready‑to‑display image
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
    
    // Extract message ID from the first message for reply
    uint8_t msgId = 0;
    if (total_bytes > 2) {
        msgId = dma_rx_buf[2]; // Message ID is at byte 2
    }
    
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
    
    // Send ACK reply for debug mode (always successful)
    reply(msgId, 0);
    
    return text_group;
}

// ───────────────────────── Utility Methods ────────────────────────
inline SPITaskHandler::Statistics SPIDriver::get_task_statistics() const {
    if (task_handler) {
        return task_handler->get_statistics();
    }
    return SPITaskHandler::Statistics{}; // Return empty stats if no task handler
}

} // namespace dice
#endif
