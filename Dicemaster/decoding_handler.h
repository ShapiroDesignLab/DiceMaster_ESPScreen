#ifndef DICE_DECODING_HANDLER_H
#define DICE_DECODING_HANDLER_H

#include <vector>
#include <queue>
#include <memory>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>

#include "media.h"
#include "protocol.h"  
#include "constants.h"
#include "ESP32DMASPIStream.h"
// Forward declare SPISlaveBuffer from the actual namespace
using SPIBuffer = ESP32DMASPI::SPISlaveBuffer;

namespace dice {

class Screen; // Forward declaration

class DecodingHandler {
private:
    // Event-driven processing queues
    QueueHandle_t raw_buffer_queue;      // Input queue for SPIBuffer objects
    SemaphoreHandle_t context_mutex;
    
    // Processing task for event-driven processing
    TaskHandle_t processing_task_handle;
    
    // Screen reference for direct enqueueing
    Screen* screen_ref;
    
    std::map<uint8_t, MediaContainer*> ongoing_transfers;
    std::map<uint8_t, uint8_t> expected_chunks; // Track expected number of chunks per image
    std::map<uint8_t, uint8_t> received_chunks; // Track received chunks per image
    std::map<uint8_t, unsigned long> transfer_start_time; // Track when transfer started
    volatile bool processing_enabled;  // Enable/disable processing
    bool initialized;  // Track if queues/mutex are created
    
    // Buffer return callback for SPI driver
    std::function<void(SPIBuffer*)> buffer_return_callback;
    
    // Queue sizes and task configuration
    static constexpr size_t RAW_BUFFER_QUEUE_SIZE = 32;    // Buffer up to 32 SPIBuffer objects
    static constexpr size_t PROCESSING_TASK_STACK = 8192;  // Stack size for processing task
    
    // Static task function for processing
    static void processing_task_function(void* parameter);
    
    // Helper function to print message bytes as hex
    void print_message_hex(const uint8_t* data, size_t size);
    
    // Helper function to return buffer to SPI driver with proper error checking
    void return_buffer_to_spi(SPIBuffer* buffer);
    
    // Event-driven processing methods (no more continuous task loop)
    MediaContainer* decode_message(const DProtocol::Message& msg);
    MediaContainer* handle(const DProtocol::TextBatch& tb);
    MediaContainer* handle(const DProtocol::ImageStart& is);
    MediaContainer* handle(const DProtocol::ImageChunk& ic);
    
public:
    DecodingHandler();
    ~DecodingHandler();
    
    // Initialize the decoding handler with screen reference for direct enqueueing
    bool initialize(Screen* screen);
    
    // Set callback for returning buffers to SPI driver when processing is complete
    void set_buffer_return_callback(std::function<void(SPIBuffer*)> callback) {
        buffer_return_callback = callback;
    }
    
    // Fast enqueue: Add SPIBuffer to processing queue (called from SPI callback)
    // Returns true if successfully queued, false if queue is full
    // Now triggers event-driven processing via notification
    bool enqueue_raw_buffer(SPIBuffer* buffer);

    // Event-driven processing: Process all available data (called from notified task)
    void process_available_data();
    
    // Get decoded media containers (deprecated - now directly enqueues to screen)
    std::vector<MediaContainer*> get_decoded_media();
    
    // Get processing statistics
    struct Statistics {
        size_t raw_chunks_received;
        size_t messages_decoded;
        size_t decode_failures;
        size_t raw_queue_overflows;
        size_t current_raw_queue_depth;
        size_t media_enqueued_to_screen;  // New stat: media sent directly to screen
        size_t total_bytes_processed;
        size_t last_chunk_size;
    } stats;
    
    Statistics get_statistics() const { return stats; }
    void reset_statistics();
    
    // Enable/disable processing control
    void enable_processing() { processing_enabled = true; }
    void disable_processing() { processing_enabled = false; }
    bool is_processing_enabled() const { return processing_enabled; }
};

// Implementation
inline DecodingHandler::DecodingHandler() 
    : raw_buffer_queue(nullptr)
    , context_mutex(nullptr)
    , processing_task_handle(nullptr)
    , screen_ref(nullptr)
    , processing_enabled(false)
    , initialized(false) 
{
    memset(&stats, 0, sizeof(stats));
}

inline DecodingHandler::~DecodingHandler() {
    // Tasks and resources persist for lifetime of application
    // No manual cleanup needed since ESP32 will reset
}

inline bool DecodingHandler::initialize(Screen* screen) {
    if (initialized) {
        return true;
    }
    
    if (!screen) {
        Serial.println("[DECODE] ERROR: Screen reference is null");
        return false;
    }
    
    screen_ref = screen;
    
    // Create FreeRTOS components
    raw_buffer_queue = xQueueCreate(RAW_BUFFER_QUEUE_SIZE, sizeof(SPIBuffer*));
    if (!raw_buffer_queue) {
        Serial.println("[DECODE] Failed to create raw buffer queue");
        return false;
    }
    
    context_mutex = xSemaphoreCreateMutex();
    if (!context_mutex) {
        Serial.println("[DECODE] Failed to create mutex");
        vQueueDelete(raw_buffer_queue);
        raw_buffer_queue = nullptr;
        return false;
    }
    
    // Create processing task
    BaseType_t task_result = xTaskCreate(
        processing_task_function,
        "DecodingProcessor",
        PROCESSING_TASK_STACK,
        this,
        4, // Medium priority - below SPI but above screen updates
        &processing_task_handle
    );
    
    if (task_result != pdPASS) {
        Serial.println("[DECODE] Failed to create processing task");
        vSemaphoreDelete(context_mutex);
        vQueueDelete(raw_buffer_queue);
        context_mutex = nullptr;
        raw_buffer_queue = nullptr;
        return false;
    }
    
    initialized = true;
    processing_enabled = true;
    Serial.println("[DECODE] Initialized event-driven handler with dedicated processing task");
    return true;
}

inline bool DecodingHandler::enqueue_raw_buffer(SPIBuffer* buffer) {
    if (!initialized || !raw_buffer_queue || !buffer || buffer->rx_size == 0 || !processing_enabled) {
        Serial.println("[DECODE] DEBUG: enqueue_raw_buffer failed - initialized:" + String(initialized) + 
                      ", has_queue:" + String(raw_buffer_queue != nullptr) + 
                      ", has_buffer:" + String(buffer != nullptr) + 
                      ", buffer_size:" + String(buffer ? buffer->rx_size : 0) + 
                      ", processing_enabled:" + String(processing_enabled));
        return false;
    }
    
    // Serial.println("[DECODE] DEBUG: Enqueueing buffer ID " + String(buffer->id) + 
    //               " with " + String(buffer->rx_size) + " bytes, queue depth: " + 
    //               String(uxQueueMessagesWaiting(raw_buffer_queue)));
    
    // Try to queue the buffer pointer (non-blocking)
    if (xQueueSend(raw_buffer_queue, &buffer, 0) != pdTRUE) {
        Serial.println("[DECODE] ERROR: Raw buffer queue full - dropping buffer ID " + String(buffer->id) + 
                      " with " + String(buffer->rx_size) + " bytes");
        stats.raw_queue_overflows++;
        return false;
    }
    
    stats.raw_chunks_received++;
    
    // Notify processing task that new data is available
    if (processing_task_handle) {
        xTaskNotifyGive(processing_task_handle);
    } else {
        Serial.println("[DECODE] ERROR: Processing task handle is null, cannot notify");
    }
    
    return true;
}

// Event-driven processing: Process all available buffer objects (called when notified)
inline void DecodingHandler::process_available_data() {
    if (!initialized || !raw_buffer_queue || !processing_enabled) {
        Serial.println("[DECODE] DEBUG: process_available_data failed - initialized:" + String(initialized) + 
                      ", has_queue:" + String(raw_buffer_queue != nullptr) + 
                      ", processing_enabled:" + String(processing_enabled));
        return;
    }
    
    // Serial.println("[DECODE] DEBUG: Starting data processing, queue depth: " + 
    //               String(uxQueueMessagesWaiting(raw_buffer_queue)));
    
    size_t buffers_processed = 0;
    size_t total_bytes = 0;
    
    // Process all available buffers in queue (non-blocking, event-driven)
    SPIBuffer* buffer;
    while (xQueueReceive(raw_buffer_queue, &buffer, 0) == pdTRUE) {
        if (!buffer) continue;

        try {
            // Serial.println("[DECODE] DEBUG: Processing buffer ID " + String(buffer->id) + 
            //               " with " + String(buffer->rx_size) + " bytes");
            
            // Print first 16 and last 16 bytes of the received message
            // Serial.print("[DECODE] Raw data (first 16 bytes): ");
            // for (size_t i = 0; i < min((size_t)16, buffer->rx_size); i++) {
            //     Serial.printf("%02X ", buffer->rx_buffer[i]);
            // }
            // Serial.println();
            
            // Each SPI buffer should contain exactly one complete message
            DProtocol::Message msg;
            ErrorCode result = DProtocol::decode(buffer->rx_buffer, buffer->rx_size, msg);

            if (result != ErrorCode::SUCCESS) {
                Serial.println("[DECODE] ERROR: Failed to decode SPI buffer ID " + String(buffer->id) + 
                              ": " + String(static_cast<int>(result)));
                stats.decode_failures++;
                
                // Return buffer to SPI driver for requeuing even on decode failure
                return_buffer_to_spi(buffer);
                continue;
            }

            // Serial.println("[DECODE] DEBUG: Successfully decoded message, tag: " + 
            //               String(static_cast<uint8_t>(msg.payload.tag)));

            // Successfully decoded the message
            MediaContainer* decoded_media = decode_message(msg);
            if (!decoded_media) {
                // Serial.println("[DECODE] DEBUG: decode_message returned null (still processing multi-chunk media)");
                stats.messages_decoded++;
                
                // Return buffer to SPI driver for requeuing
                return_buffer_to_spi(buffer);
                continue;
            }
            
            // Serial.println("[DECODE] DEBUG: Media ready! Type: " + String(static_cast<int>(decoded_media->get_media_type())) + 
            //               ", attempting to enqueue to screen");

            if (!screen_ref) {
                Serial.println("[DECODE] ERROR: Screen reference is null");
                delete decoded_media;
                stats.messages_decoded++;
                
                // Return buffer to SPI driver for requeuing
                return_buffer_to_spi(buffer);
                continue;
            }

            if (!screen_ref->enqueue(decoded_media)) {
                Serial.println("[DECODE] ERROR: Failed to enqueue media to screen");
                delete decoded_media;
            } else {
                // Serial.println("[DECODE] SUCCESS: Media enqueued to screen successfully");
                stats.media_enqueued_to_screen++;
            }

            stats.messages_decoded++;
        } catch (...) {
            Serial.println("[DECODE] ERROR: Exception during buffer processing");
            stats.decode_failures++;
            
            // Return buffer to SPI driver for requeuing even on exception
            return_buffer_to_spi(buffer);
            
            // Update basic statistics and continue to next buffer
            total_bytes += buffer->rx_size;
            buffers_processed++;
            stats.total_bytes_processed += buffer->rx_size;
            stats.last_chunk_size = buffer->rx_size;
            continue;
        }

        // Update basic statistics
        total_bytes += buffer->rx_size;
        buffers_processed++;
        stats.total_bytes_processed += buffer->rx_size;
        stats.last_chunk_size = buffer->rx_size;

        // Serial.println("[DECODE] Processed buffer ID " + String(buffer->id) + 
        //               " with " + String(buffer->rx_size) + " bytes, returning buffer to SPI");

        // Return buffer to SPI driver for requeuing
        return_buffer_to_spi(buffer);
    }

    // Simple logging for multiple buffers
    // if (buffers_processed > 1) {
    //     Serial.println("[DECODE] Processed " + String(buffers_processed) + 
    //                    " buffers, " + String(total_bytes) + "B");
    // }
    
    // Update queue depth statistics
    stats.current_raw_queue_depth = uxQueueMessagesWaiting(raw_buffer_queue);
}

inline void DecodingHandler::reset_statistics() {
    memset(&stats, 0, sizeof(stats));
}

// Helper function to print first 16 and last 16 bytes of message as hex
inline void DecodingHandler::print_message_hex(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        Serial.println("[DECODE] Message hex: <empty or null data>");
        return;
    }
    
    String hex_output = "[DECODE] Message hex: ";
    
    // Print first 16 bytes
    size_t first_bytes = min(size, (size_t)16);
    for (size_t i = 0; i < first_bytes; i++) {
        if (data[i] < 0x10) hex_output += "0";
        hex_output += String(data[i], HEX);
        if (i < first_bytes - 1) hex_output += " ";
    }
    
    // If message is longer than 16 bytes, show last 16 bytes
    if (size > 16) {
        if (size <= 32) {
            // For messages 17-32 bytes, just add remaining bytes
            hex_output += " ";
            for (size_t i = 16; i < size; i++) {
                if (data[i] < 0x10) hex_output += "0";
                hex_output += String(data[i], HEX);
                if (i < size - 1) hex_output += " ";
            }
        } else {
            // For messages > 32 bytes, show ... and last 16 bytes
            hex_output += " ... ";
            size_t last_start = size - 16;
            for (size_t i = last_start; i < size; i++) {
                if (data[i] < 0x10) hex_output += "0";
                hex_output += String(data[i], HEX);
                if (i < size - 1) hex_output += " ";
            }
        }
    }
    
    hex_output += " (size: " + String(size) + " bytes)";
    Serial.println(hex_output);
}

// Helper function to return buffer to SPI driver with proper error checking
inline void DecodingHandler::return_buffer_to_spi(SPIBuffer* buffer) {
    if (buffer_return_callback) {
        buffer_return_callback(buffer);
    } else {
        Serial.println("[DECODE] ERROR: No buffer return callback set");
    }
}

inline MediaContainer* DecodingHandler::decode_message(const DProtocol::Message& msg) {
    if (xSemaphoreTake(context_mutex, portMAX_DELAY) != pdTRUE) {
        Serial.println("[DECODE] ERROR: Failed to acquire context mutex");
        return nullptr;
    }
    
    // Serial.println("[DECODE] DEBUG: decode_message called with tag: " + String(static_cast<uint8_t>(msg.payload.tag)));
    
    MediaContainer* result = nullptr;
    
    switch (msg.payload.tag) {
        case DProtocol::TAG_TEXT_BATCH:
            // Serial.println("[DECODE] DEBUG: Processing TEXT_BATCH");
            result = handle(msg.payload.u.textBatch);
            break;
            
        case DProtocol::TAG_IMAGE_START:
            // Serial.println("[DECODE] DEBUG: Processing IMAGE_START for image ID " + String(msg.payload.u.imageStart.imgId));
            result = handle(msg.payload.u.imageStart);
            break;
            
        case DProtocol::TAG_IMAGE_CHUNK:
            // Serial.println("[DECODE] DEBUG: Processing IMAGE_CHUNK for image ID " + String(msg.payload.u.imageChunk.imgId) + 
            //               ", chunk ID " + String(msg.payload.u.imageChunk.chunkId));
            result = handle(msg.payload.u.imageChunk);
            break;
            
        case DProtocol::TAG_BACKLIGHT_ON:
            // Serial.println("[DECODE] DEBUG: Processing BACKLIGHT_ON");
            // Handle backlight control (no media container needed)
            break;
            
        case DProtocol::TAG_BACKLIGHT_OFF:
            // Serial.println("[DECODE] DEBUG: Processing BACKLIGHT_OFF");
            // Handle backlight control (no media container needed)
            break;
            
        default:
            Serial.println("[DECODE] ERROR: Unknown tag: " + String(static_cast<uint8_t>(msg.payload.tag)));
            break;
    }
    
    // if (result) {
    //     Serial.println("[DECODE] DEBUG: decode_message returning media, type: " + String(static_cast<int>(result->get_media_type())) + 
    //                   ", status: " + String(static_cast<int>(result->get_status())));
    // } else {
    //     Serial.println("[DECODE] DEBUG: decode_message returning null");
    // }
    
    xSemaphoreGive(context_mutex);
    return result;
}

// Message handlers (adapted from spi_task_handler.h)
inline MediaContainer* DecodingHandler::handle(const DProtocol::TextBatch& tb) {
    // Serial.println("[DECODE] DEBUG: Creating TextGroup with " + String(tb.itemCount) + " items, " +
    //               "bgColor: 0x" + String(tb.bgColor, HEX) + ", rotation: " + String(tb.rotation));
    
    auto* group = new TextGroup(0, tb.bgColor, 0xFFFF);
    
    for (uint8_t i = 0; i < tb.itemCount; ++i) {
        const auto& item = tb.items[i];
        String text_str = String(item.text).substring(0, item.len);
        
        // Serial.println("[DECODE] DEBUG: Adding text item " + String(i) + ": '" + text_str + 
        //               "' at (" + String(item.x) + "," + String(item.y) + "), " +
        //               "font: " + String(item.font) + ", color: 0x" + String(item.color, HEX));
        
        Text* text_obj = new Text(text_str, 5000, static_cast<FontID>(item.font), item.x, item.y, item.color);
        group->add_member(text_obj);
    }
    
    group->set_rotation(static_cast<Rotation>(tb.rotation));
    // Serial.println("[DECODE] DEBUG: TextGroup created successfully, status: " + String(static_cast<int>(group->get_status())));
    return group;
}

inline MediaContainer* DecodingHandler::handle(const DProtocol::ImageStart& is) {
    // Serial.println("[DECODE] DEBUG: ImageStart - ID: " + String(is.imgId) + 
    //               ", totalSize: " + String(is.totalSize) + "B, " +
    //               "numChunks: " + String(is.numChunks) + ", " +
    //               "delayMs: " + String(is.delayMs) + ", " +
    //               "fmtRes: 0x" + String(is.fmtRes, HEX) + ", " +
    //               "rotation: " + String(is.rotation));
    
    // Clean up any existing transfer with the same ID
    if (ongoing_transfers.count(is.imgId)) {
        Serial.println("[DECODE] WARNING: Replacing transfer ID " + String(is.imgId));
        delete ongoing_transfers[is.imgId];
        ongoing_transfers.erase(is.imgId);
        expected_chunks.erase(is.imgId);
        received_chunks.erase(is.imgId);
        transfer_start_time.erase(is.imgId);
    }
    
    ImageFormat fmt = static_cast<ImageFormat>(is.fmtRes >> 4);
    ImageResolution res = static_cast<ImageResolution>(is.fmtRes & 0x0F);
    
    // Serial.println("[DECODE] DEBUG: Parsed format: " + String(static_cast<int>(fmt)) + 
    //               ", resolution: " + String(static_cast<int>(res)));
    
    // Only log for larger images or multiple chunks
    // if (is.totalSize > 10000 || is.numChunks > 5) {
    //     Serial.println("[DECODE] Image ID " + String(is.imgId) + ": " + 
    //                    String(is.totalSize) + "B, " + String(is.numChunks) + " chunks (includes embedded chunk 0)");
    // }
    
    // Record expected chunks for this image
    expected_chunks[is.imgId] = is.numChunks;
    received_chunks[is.imgId] = 0;
    transfer_start_time[is.imgId] = millis();
    
    // Create image with new constructor that includes chunk count and rotation
    Rotation rot = static_cast<Rotation>(is.rotation);
    Image* img = new Image(is.imgId, fmt, res, is.totalSize, is.delayMs, is.numChunks, rot);
    if (img->get_status() == MediaStatus::EXPIRED) {
        Serial.println("[DECODE] ERROR: Failed to create image for ID " + String(is.imgId));
        expected_chunks.erase(is.imgId);
        received_chunks.erase(is.imgId);
        delete img;
        return nullptr;
    }
    
    // Serial.println("[DECODE] DEBUG: Created Image object, status: " + String(static_cast<int>(img->get_status())));
    
    // Process embedded chunk 0 if present
    if (is.embeddedChunk.length > 0 && is.embeddedChunk.data != nullptr) {
        // Serial.println("[DECODE] DEBUG: Processing embedded chunk 0 for ID " + String(is.imgId) + 
        //                " (" + String(is.embeddedChunk.length) + " bytes)");
        img->add_chunk_with_id(is.embeddedChunk.data, is.embeddedChunk.length, 0);
        received_chunks[is.imgId] = 1; // First chunk received
        // Serial.println("[DECODE] DEBUG: Added embedded chunk 0, image status: " + String(static_cast<int>(img->get_status())));
    } else {
        // Serial.println("[DECODE] WARNING: No embedded chunk 0 data in ImageStart for ID " + String(is.imgId));
        received_chunks[is.imgId] = 0; // No chunks received yet
    }
    
    img->set_rotation(static_cast<Rotation>(is.rotation));
    ongoing_transfers[is.imgId] = img;
    
    // Check if this was the only chunk (single-chunk image)
    if (is.numChunks == 1) {
        // Serial.println("[DECODE] DEBUG: Single-chunk image complete for ID " + String(is.imgId) + 
        //               ", final status: " + String(static_cast<int>(img->get_status())));
        MediaContainer* completed = img;
        ongoing_transfers.erase(is.imgId);
        expected_chunks.erase(is.imgId);
        received_chunks.erase(is.imgId);
        transfer_start_time.erase(is.imgId);
        return completed;
    }
    
    // Serial.println("[DECODE] DEBUG: Multi-chunk image started for ID " + String(is.imgId) + 
    //               ", waiting for " + String(is.numChunks - 1) + " more chunks");
    
    return nullptr;
}

inline MediaContainer* DecodingHandler::handle(const DProtocol::ImageChunk& ic) {
    auto it = ongoing_transfers.find(ic.imgId);
    if (it == ongoing_transfers.end()) {
        Serial.println("[DECODE] ERROR: ImageChunk for unknown image ID: " + String(ic.imgId));
        return nullptr;
    }
    
    // Track received chunks (count of chunks received so far)
    uint8_t expected_total = expected_chunks.count(ic.imgId) ? expected_chunks[ic.imgId] : 0;
    uint8_t received_count = received_chunks[ic.imgId]; // Current count before increment
    // Note: chunk IDs start from 1 now since chunk 0 is embedded in ImageStart
    // Since we start with received_count = 1 (chunk 0 already processed), 
    // the first separate chunk should be ID 1, second should be ID 2, etc.
    uint8_t expected_chunk_id = received_count; // Expected chunk ID = current received count
    
    // Debug: Track chunk sequence and detect gaps
    // Serial.println("[CHUNK-SEQ] Image " + String(ic.imgId) + 
    //                " - ChunkID: " + String(ic.chunkId) + 
    //                " (expected: " + String(expected_chunk_id) + ")" +
    //                ", Length: " + String(ic.length) + " bytes" +
    //                ", Progress: " + String(received_count + 1) + "/" + String(expected_total));
    
    // Check for sequence gaps (chunks should be sequential starting from 1)
    // if (ic.chunkId != expected_chunk_id) {
    //     Serial.println("[CHUNK-GAP] Image " + String(ic.imgId) + 
    //                    " - Expected chunk " + String(expected_chunk_id) + 
    //                    " but got " + String(ic.chunkId) + "!");
    //     if (ic.chunkId > expected_chunk_id) {
    //         Serial.println("[CHUNK-MISSING] Image " + String(ic.imgId) + 
    //                        " is missing chunks " + String(expected_chunk_id) + 
    //                        " through " + String(ic.chunkId - 1));
    //     }
    // }
    
    // Increment received count after processing
    received_chunks[ic.imgId]++;
    
    Image* img = static_cast<Image*>(it->second);
    if (img->get_status() == MediaStatus::EXPIRED) {
        Serial.println("[DECODE] ERROR: Cannot add chunk to expired image ID: " + String(ic.imgId));
        return nullptr;
    }
    
    // Add the chunk data with ID tracking
    img->add_chunk_with_id(ic.data, ic.length, ic.chunkId);
    // Serial.println("[DECODE] DEBUG: Added chunk " + String(ic.chunkId) + " to image " + String(ic.imgId) + 
    //               ", image status: " + String(static_cast<int>(img->get_status())));
    
    // Check if transfer is complete (all chunks received)
    if (received_chunks[ic.imgId] >= expected_total) {
        Serial.println("[DECODE] SUCCESS: Image ID " + String(ic.imgId) + " transfer complete: " + 
                       String(received_chunks[ic.imgId]) + "/" + String(expected_total) + " chunks, " +
                       "final status: " + String(static_cast<int>(img->get_status())));
        
        MediaContainer* complete = img;
        ongoing_transfers.erase(it);
        expected_chunks.erase(ic.imgId);
        received_chunks.erase(ic.imgId);
        transfer_start_time.erase(ic.imgId);
        // Serial.println("[DECODE] Image ID " + String(ic.imgId) + " returning complete");
        return complete;
    }
    
    Serial.println("[DECODE] DEBUG: Image ID " + String(ic.imgId) + " still waiting for more chunks: " +
                  String(received_chunks[ic.imgId]) + "/" + String(expected_total));
    
    return nullptr;
}

// Static processing task function
inline void DecodingHandler::processing_task_function(void* parameter) {
    DecodingHandler* handler = static_cast<DecodingHandler*>(parameter);
    Serial.println("[DECODE-TASK] Processing task started, waiting for data notifications...");
    
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        if (!handler) {
            Serial.println("[DECODE-TASK] ERROR: Handler pointer is null");
            continue;
        }
        handler->process_available_data();
    }
}

} // namespace dice

// Include screen.h after class definitions to avoid circular dependencies
#include "screen.h"

#endif // DICE_DECODING_HANDLER_H
