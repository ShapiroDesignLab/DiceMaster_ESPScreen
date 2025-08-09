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

namespace dice {

class Screen; // Forward declaration

// Structure to hold raw SPI data copied to PSRAM
struct SPIDataChunk {
    size_t size;
    uint8_t* data;
    uint32_t timestamp;
    
    // Default constructor for taking ownership of existing buffer
    SPIDataChunk() : size(0), data(nullptr), timestamp(0) {}
    
    SPIDataChunk(size_t sz) : size(sz), timestamp(millis()) {
        // Allocate in PSRAM if available, otherwise regular heap
        if (psramFound() && sz > 1024) { // Only use PSRAM for larger chunks
            data = (uint8_t*)ps_malloc(sz);
            if (!data) {
                Serial.println("[DECODE] PSRAM allocation failed, trying heap for size: " + String(sz));
                data = (uint8_t*)malloc(sz);
            }
        } else {
            data = (uint8_t*)malloc(sz);
        }
        
        if (!data) {
            Serial.println("[DECODE] Memory allocation completely failed for size: " + String(sz));
            size = 0;
        }
    }
    
    ~SPIDataChunk() {
        // Don't free data in destructor since it may be owned by SPI driver
        // The buffer return callback handles returning the buffer to the SPI driver
        data = nullptr;
    }
    
    // Non-copyable but movable
    SPIDataChunk(const SPIDataChunk&) = delete;
    SPIDataChunk& operator=(const SPIDataChunk&) = delete;
    
    SPIDataChunk(SPIDataChunk&& other) noexcept 
        : size(other.size), data(other.data), timestamp(other.timestamp) {
        other.data = nullptr;
        other.size = 0;
    }
    
    SPIDataChunk& operator=(SPIDataChunk&& other) noexcept {
        if (this != &other) {
            if (data) free(data);
            size = other.size;
            data = other.data;
            timestamp = other.timestamp;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }
};

class DecodingHandler {
private:
    // Event-driven processing queues
    QueueHandle_t raw_data_queue;        // Input queue for raw SPI data
    SemaphoreHandle_t context_mutex;
    
    // Screen reference for direct enqueueing
    Screen* screen_ref;
    
    std::map<uint8_t, MediaContainer*> ongoing_transfers;
    std::map<uint8_t, uint8_t> expected_chunks; // Track expected number of chunks per image
    std::map<uint8_t, uint8_t> received_chunks; // Track received chunks per image
    std::map<uint8_t, unsigned long> transfer_start_time; // Track when transfer started
    volatile bool processing_enabled;  // Enable/disable processing
    bool initialized;  // Track if queues/mutex are created
    
    // Buffer return callback for SPI driver
    std::function<void(uint8_t*)> buffer_return_callback;
    
    // Queue sizes
    static constexpr size_t RAW_DATA_QUEUE_SIZE = 32;      // Buffer up to 32 raw SPI chunks (increased for better throughput)
    
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
    void set_buffer_return_callback(std::function<void(uint8_t*)> callback) {
        buffer_return_callback = callback;
    }
    
    // Fast enqueue: Add raw SPI data chunk to processing queue (called from SPI callback)
    // Returns true if successfully queued, false if queue is full
    // Now triggers event-driven processing via notification
    bool enqueue_raw_data(const uint8_t* data, size_t size);
    
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
};

// Implementation
inline DecodingHandler::DecodingHandler() 
    : raw_data_queue(nullptr)
    , context_mutex(nullptr)
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
        Serial.println("[DECODE] Already initialized");
        return true;
    }
    
    if (!screen) {
        Serial.println("[DECODE] ERROR: Screen reference is null");
        return false;
    }
    
    screen_ref = screen;
    
    // Create FreeRTOS components
    raw_data_queue = xQueueCreate(RAW_DATA_QUEUE_SIZE, sizeof(SPIDataChunk*));
    if (!raw_data_queue) {
        Serial.println("[DECODE] Failed to create raw data queue");
        return false;
    }
    
    context_mutex = xSemaphoreCreateMutex();
    if (!context_mutex) {
        Serial.println("[DECODE] Failed to create mutex");
        vQueueDelete(raw_data_queue);
        raw_data_queue = nullptr;
        return false;
    }
    
    initialized = true;
    processing_enabled = true; // Enable processing immediately since we use synchronous calls
    Serial.println("[DECODE] Initialized event-driven handler with direct screen enqueueing");
    return true;
}

inline bool DecodingHandler::enqueue_raw_data(const uint8_t* data, size_t size) {
    if (!initialized || !raw_data_queue || !data || size == 0 || !processing_enabled) {
        return false;
    }
    
    // Create chunk wrapper that takes ownership of the existing buffer
    SPIDataChunk* chunk = new(std::nothrow) SPIDataChunk();
    if (!chunk) {
        stats.raw_queue_overflows++;
        return false;
    }
    
    // Take ownership of the existing buffer (no copy)
    chunk->data = const_cast<uint8_t*>(data);
    chunk->size = size;
    chunk->timestamp = millis();
    
    // Try to queue (non-blocking)
    if (xQueueSend(raw_data_queue, &chunk, 0) != pdTRUE) {
        Serial.println("[DECODE] ERROR: Raw data queue full - dropping " + String(size) + " byte chunk");
        delete chunk;
        stats.raw_queue_overflows++;
        return false;
    }
    
    stats.raw_chunks_received++;
    
    // No need to notify target task since processing will be called synchronously
    return true;
}

// Event-driven processing: Process all available data chunks (called when notified)
inline void DecodingHandler::process_available_data() {
    if (!initialized || !raw_data_queue || !processing_enabled) {
        return;
    }
    
    size_t chunks_processed = 0;
    size_t total_bytes = 0;
    
    // Process all available chunks in queue (non-blocking, event-driven)
    SPIDataChunk* chunk;
    while (xQueueReceive(raw_data_queue, &chunk, 0) == pdTRUE) {
        if (!chunk) continue;
        
        try {
            // Each SPI chunk should contain exactly one complete message
            DProtocol::Message msg;
            
            ErrorCode result = DProtocol::decode(
                chunk->data, 
                chunk->size, 
                msg
            );

            if (result != ErrorCode::SUCCESS) {
                Serial.println("[DECODE] Failed to decode SPI chunk: " + String(static_cast<int>(result)));
                stats.decode_failures++;
                delete chunk;
                continue;
            }
            
            // Successfully decoded the message
            MediaContainer* decoded_media = decode_message(msg);
            
            if (decoded_media && decoded_media->get_status() == MediaStatus::READY) {
                // Directly enqueue to screen - we need to include screen.h for this to work
                // The include is placed at the end of this file to avoid circular dependencies
                Serial.printf("[DECODE] Enqueueing decoded media type %d to screen\n", (int)decoded_media->get_media_type());
                if (screen_ref) {
                    extern bool screen_enqueue_wrapper(Screen* screen, MediaContainer* media);
                    if (screen_enqueue_wrapper(screen_ref, decoded_media)) {
                        stats.media_enqueued_to_screen++;
                        Serial.println("[DECODE] Successfully enqueued media to screen");
                    } else {
                        Serial.println("[DECODE] ERROR: Failed to enqueue media to screen");
                        delete decoded_media;
                    }
                } else {
                    Serial.println("[DECODE] ERROR: Screen reference is null");
                    delete decoded_media;
                }
            }
            
            stats.messages_decoded++;
        } catch (...) {
            Serial.println("[DECODE] ERROR: Exception during chunk processing");
            stats.decode_failures++;
        }
        
        // Update basic statistics
        total_bytes += chunk->size;
        chunks_processed++;
        stats.total_bytes_processed += chunk->size;
        stats.last_chunk_size = chunk->size;
        
        // Return buffer to SPI driver for requeuing
        if (buffer_return_callback && chunk->data) {
            buffer_return_callback(chunk->data);
        }
        
        // Clean up chunk wrapper (but not the data, which is now owned by SPI driver)
        delete chunk;
    }
    
    // Simple logging for multiple chunks
    if (chunks_processed > 1) {
        Serial.println("[DECODE] Processed " + String(chunks_processed) + 
                       " chunks, " + String(total_bytes) + "B");
    }
    
    // Update queue depth statistics
    stats.current_raw_queue_depth = uxQueueMessagesWaiting(raw_data_queue);
}

inline std::vector<MediaContainer*> DecodingHandler::get_decoded_media() {
    // NOTE: This method is deprecated since we now directly enqueue to screen
    // Kept for API compatibility but returns empty vector
    std::vector<MediaContainer*> result;
    Serial.println("[DECODE] WARNING: get_decoded_media() called but media is now directly enqueued to screen");
    return result;
}

inline void DecodingHandler::reset_statistics() {
    memset(&stats, 0, sizeof(stats));
}

inline MediaContainer* DecodingHandler::decode_message(const DProtocol::Message& msg) {
    if (xSemaphoreTake(context_mutex, portMAX_DELAY) != pdTRUE) {
        return nullptr;
    }
    
    MediaContainer* result = nullptr;
    
    switch (msg.payload.tag) {
        case DProtocol::TAG_TEXT_BATCH:
            result = handle(msg.payload.u.textBatch);
            break;
            
        case DProtocol::TAG_IMAGE_START:
            result = handle(msg.payload.u.imageStart);
            break;
            
        case DProtocol::TAG_IMAGE_CHUNK:
            result = handle(msg.payload.u.imageChunk);
            break;
            
        case DProtocol::TAG_BACKLIGHT_ON:
        case DProtocol::TAG_BACKLIGHT_OFF:
            // Handle backlight control (no media container needed)
            break;
            
        default:
            Serial.println("[DECODE] Unknown tag: " + String(static_cast<uint8_t>(msg.payload.tag)));
            break;
    }
    
    xSemaphoreGive(context_mutex);
    return result;
}

// Message handlers (adapted from spi_task_handler.h)
inline MediaContainer* DecodingHandler::handle(const DProtocol::TextBatch& tb) {
    auto* group = new TextGroup(0, tb.bgColor, 0xFFFF);
    
    for (uint8_t i = 0; i < tb.itemCount; ++i) {
        const auto& item = tb.items[i];
        String text_str = String(item.text).substring(0, item.len);
        
        Text* text_obj = new Text(text_str, 5000, static_cast<FontID>(item.font), item.x, item.y, item.color);
        group->add_member(text_obj);
    }
    
    group->set_rotation(static_cast<Rotation>(tb.rotation));
    return group;
}

inline MediaContainer* DecodingHandler::handle(const DProtocol::ImageStart& is) {
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
    
    // Only log for larger images or multiple chunks
    if (is.totalSize > 10000 || is.numChunks > 5) {
        Serial.println("[DECODE] Image ID " + String(is.imgId) + ": " + 
                       String(is.totalSize) + "B, " + String(is.numChunks) + " chunks (includes embedded chunk 0)");
    }
    
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
    
    // Process embedded chunk 0 if present
    if (is.embeddedChunk.length > 0 && is.embeddedChunk.data != nullptr) {
        Serial.println("[DECODE] Processing embedded chunk 0 for ID " + String(is.imgId) + 
                       " (" + String(is.embeddedChunk.length) + " bytes)");
        img->add_chunk_with_id(is.embeddedChunk.data, is.embeddedChunk.length, 0);
        received_chunks[is.imgId] = 1; // First chunk received
    } else {
        Serial.println("[DECODE] WARNING: No embedded chunk 0 data in ImageStart for ID " + String(is.imgId));
        received_chunks[is.imgId] = 0; // No chunks received yet
    }
    
    img->set_rotation(static_cast<Rotation>(is.rotation));
    ongoing_transfers[is.imgId] = img;
    
    // Check if this was the only chunk (single-chunk image)
    if (is.numChunks == 1) {
        Serial.println("[DECODE] Single-chunk image complete for ID " + String(is.imgId));
        MediaContainer* completed = img;
        ongoing_transfers.erase(is.imgId);
        expected_chunks.erase(is.imgId);
        received_chunks.erase(is.imgId);
        transfer_start_time.erase(is.imgId);
        return completed;
    }
    
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
    Serial.println("[CHUNK-SEQ] Image " + String(ic.imgId) + 
                   " - ChunkID: " + String(ic.chunkId) + 
                   " (expected: " + String(expected_chunk_id) + ")" +
                   ", Length: " + String(ic.length) + " bytes" +
                   ", Progress: " + String(received_count + 1) + "/" + String(expected_total));
    
    // Check for sequence gaps (chunks should be sequential starting from 1)
    if (ic.chunkId != expected_chunk_id) {
        Serial.println("[CHUNK-GAP] Image " + String(ic.imgId) + 
                       " - Expected chunk " + String(expected_chunk_id) + 
                       " but got " + String(ic.chunkId) + "!");
        if (ic.chunkId > expected_chunk_id) {
            Serial.println("[CHUNK-MISSING] Image " + String(ic.imgId) + 
                           " is missing chunks " + String(expected_chunk_id) + 
                           " through " + String(ic.chunkId - 1));
        }
    }
    
    // Increment received count after processing
    received_chunks[ic.imgId]++;
    
    Image* img = static_cast<Image*>(it->second);
    if (img->get_status() == MediaStatus::EXPIRED) {
        Serial.println("[DECODE] ERROR: Cannot add chunk to expired image ID: " + String(ic.imgId));
        return nullptr;
    }
    
    // Add the chunk data with ID tracking
    img->add_chunk_with_id(ic.data, ic.length, ic.chunkId);
    
    // Check if transfer is complete (all chunks received)
    if (received_chunks[ic.imgId] >= expected_total) {
        Serial.println("[DECODE] Image ID " + String(ic.imgId) + " transfer complete: " + 
                       String(received_chunks[ic.imgId]) + "/" + String(expected_total) + " chunks");
        
        MediaContainer* complete = img;
        ongoing_transfers.erase(it);
        expected_chunks.erase(ic.imgId);
        received_chunks.erase(ic.imgId);
        transfer_start_time.erase(ic.imgId);
        return complete;
    }
    
    return nullptr;
}

} // namespace dice

// Include screen.h after class definitions to avoid circular dependencies
#include "screen.h"

// Wrapper function to call screen->enqueue() from decoding handler
inline bool screen_enqueue_wrapper(dice::Screen* screen, dice::MediaContainer* media) {
    if (!screen || !media) return false;
    return screen->enqueue(media);
}

#endif // DICE_DECODING_HANDLER_H
