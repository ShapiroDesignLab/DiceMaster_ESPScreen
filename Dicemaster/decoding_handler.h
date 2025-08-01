#ifndef DICE_DECODING_HANDLER_H
#define DICE_DECODING_HANDLER_H

#include <vector>
#include <queue>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>

#include "media.h"
#include "protocol.h"
#include "constants.h"

namespace dice {

// Structure to hold raw SPI data copied to PSRAM
struct SPIDataChunk {
    size_t size;
    uint8_t* data;
    uint32_t timestamp;
    
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
        if (data) {
            free(data);
            data = nullptr;
        }
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
    // FreeRTOS components
    TaskHandle_t decoding_task_handle;
    QueueHandle_t raw_data_queue;        // Input queue for raw SPI data
    QueueHandle_t decoded_media_queue;   // Output queue for decoded MediaContainers
    SemaphoreHandle_t context_mutex;
    
    // Processing state
    std::map<uint8_t, MediaContainer*> ongoing_transfers;
    std::map<uint8_t, uint8_t> expected_chunks; // Track expected number of chunks per image
    std::map<uint8_t, uint8_t> received_chunks; // Track received chunks per image
    std::map<uint8_t, unsigned long> transfer_start_time; // Track when transfer started
    bool task_running;
    bool initialized;  // Track if queues/mutex are created
    
    // Queue sizes
    static constexpr size_t RAW_DATA_QUEUE_SIZE = 16;      // Buffer up to 16 raw SPI chunks
    static constexpr size_t DECODED_MEDIA_QUEUE_SIZE = 8;  // Buffer up to 8 decoded media objects
    static constexpr size_t DECODING_STACK_SIZE = 12288;   // Stack size for decoding task (increased for JPEG decoding)
    
    // Static task function (required by FreeRTOS)
    static void decoding_task_function(void* parameter);
    
    // Internal processing methods
    void process_raw_data_internal();
    MediaContainer* decode_message(const DProtocol::Message& msg);
    bool try_sync_recovery(const uint8_t* data, size_t size, size_t& recovered_offset);
    MediaContainer* handle(const DProtocol::TextBatch& tb);
    MediaContainer* handle(const DProtocol::ImageStart& is);
    MediaContainer* handle(const DProtocol::ImageChunk& ic);
    MediaContainer* handle(const DProtocol::ImageEnd& ie);
    
public:
    DecodingHandler();
    ~DecodingHandler();
    
    // Initialize the decoding handler (creates queues and mutex only)
    bool initialize();
    
    // Start the decoding task (called when first data arrives)
    bool start_task();
    
    // Shutdown the decoding handler
    void shutdown();
    
    // Fast enqueue: Add raw SPI data chunk to processing queue (called from SPI callback)
    // Returns true if successfully queued, false if queue is full
    bool enqueue_raw_data(const uint8_t* data, size_t size);
    
    // Get decoded media containers (non-blocking, called from main loop at 30Hz)
    std::vector<MediaContainer*> get_decoded_media();
    
    // Get processing statistics
    struct Statistics {
        size_t raw_chunks_received;
        size_t messages_decoded;
        size_t decode_failures;
        size_t raw_queue_overflows;
        size_t current_raw_queue_depth;
        size_t current_decoded_queue_depth;
        size_t max_decode_time_ms;
        size_t avg_decode_time_ms;
        size_t total_bytes_processed;  // Add total bytes counter
        size_t last_chunk_size;       // Size of last processed chunk
        size_t sof_marker_errors;     // Count of invalid SOF markers
        size_t header_errors;         // Count of header decode errors
        size_t payload_errors;        // Count of payload decode errors
        size_t sync_recovery_attempts; // Count of sync recovery attempts
    } stats;
    
    Statistics get_statistics() const { return stats; }
    void reset_statistics();
};

// Implementation
inline DecodingHandler::DecodingHandler() 
    : decoding_task_handle(nullptr)
    , raw_data_queue(nullptr)
    , decoded_media_queue(nullptr)
    , context_mutex(nullptr)
    , task_running(false)
    , initialized(false) 
{
    memset(&stats, 0, sizeof(stats));
}

inline DecodingHandler::~DecodingHandler() {
    shutdown();
}

inline bool DecodingHandler::initialize() {
    if (initialized) {
        Serial.println("[DECODE] Already initialized");
        return true;
    }
    
    // Create FreeRTOS components
    raw_data_queue = xQueueCreate(RAW_DATA_QUEUE_SIZE, sizeof(SPIDataChunk*));
    if (!raw_data_queue) {
        Serial.println("[DECODE] Failed to create raw data queue");
        return false;
    }
    
    decoded_media_queue = xQueueCreate(DECODED_MEDIA_QUEUE_SIZE, sizeof(MediaContainer*));
    if (!decoded_media_queue) {
        Serial.println("[DECODE] Failed to create decoded media queue");
        vQueueDelete(raw_data_queue);
        raw_data_queue = nullptr;
        return false;
    }
    
    context_mutex = xSemaphoreCreateMutex();
    if (!context_mutex) {
        Serial.println("[DECODE] Failed to create mutex");
        vQueueDelete(raw_data_queue);
        vQueueDelete(decoded_media_queue);
        raw_data_queue = nullptr;
        decoded_media_queue = nullptr;
        return false;
    }
    
    initialized = true;
    Serial.println("[DECODE] Initialized queues and mutex (task will start on first data)");
    return true;
}

inline bool DecodingHandler::start_task() {
    Serial.println("[DECODE] start_task() called, initialized=" + String(initialized) + ", task_running=" + String(task_running));
    
    if (!initialized) {
        Serial.println("[DECODE] ERROR: Cannot start task - not initialized");
        return false;
    }
    
    if (task_running) {
        Serial.println("[DECODE] Task already running");
        return true; // Already running
    }
    
    Serial.println("[DECODE] Creating FreeRTOS task...");
    
    // Create decoding task
    BaseType_t result = xTaskCreate(
        decoding_task_function,
        "DecodeProcessor",
        DECODING_STACK_SIZE,
        this,
        5, // Priority (higher priority for faster processing)
        &decoding_task_handle
    );
    
    if (result != pdPASS) {
        Serial.println("[DECODE] Failed to create decoding task, result=" + String(result));
        return false;
    }
    
    task_running = true;
    Serial.println("[DECODE] Task created successfully, handle=" + String((unsigned long)decoding_task_handle, HEX));
    
    // Give the task a moment to start
    vTaskDelay(pdMS_TO_TICKS(50));
    
    Serial.println("[DECODE] start_task() completed successfully");
    return true;
}

inline void DecodingHandler::shutdown() {
    // Stop the task first
    if (task_running) {
        task_running = false;
        
        // Delete task if it exists
        if (decoding_task_handle) {
            vTaskDelete(decoding_task_handle);
            decoding_task_handle = nullptr;
        }
    }
    
    if (!initialized) return;
    
    // Clean up ongoing transfers BEFORE deleting mutex
    if (context_mutex) {
        if (xSemaphoreTake(context_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            for (auto& pair : ongoing_transfers) {
                delete pair.second;
            }
            ongoing_transfers.clear();
            expected_chunks.clear();
            received_chunks.clear();
            transfer_start_time.clear();
            xSemaphoreGive(context_mutex);
        } else {
            Serial.println("[DECODE] Warning: Could not acquire mutex during shutdown");
            // Force cleanup without mutex protection
            for (auto& pair : ongoing_transfers) {
                delete pair.second;
            }
            ongoing_transfers.clear();
            expected_chunks.clear();
            received_chunks.clear();
            transfer_start_time.clear();
        }
    }
    
    // Clean up raw data queue
    if (raw_data_queue) {
        SPIDataChunk* chunk;
        while (xQueueReceive(raw_data_queue, &chunk, 0) == pdTRUE) {
            delete chunk;
        }
        vQueueDelete(raw_data_queue);
        raw_data_queue = nullptr;
    }
    
    // Clean up decoded media queue 
    if (decoded_media_queue) {
        MediaContainer* media;
        while (xQueueReceive(decoded_media_queue, &media, 0) == pdTRUE) {
            delete media;
        }
        vQueueDelete(decoded_media_queue);
        decoded_media_queue = nullptr;
    }

    // Delete mutex last
    if (context_mutex) {
        vSemaphoreDelete(context_mutex);
        context_mutex = nullptr;
    }
    
    initialized = false;
    Serial.println("[DECODE] Shutdown complete");
}

inline bool DecodingHandler::enqueue_raw_data(const uint8_t* data, size_t size) {
    if (!initialized || !raw_data_queue || !data || size == 0) {
        return false;
    }
    
    // Lazy task startup - start task when first data arrives
    if (!task_running) {
        Serial.println("[DECODE] First data received, starting task...");
        if (!start_task()) {
            Serial.println("[DECODE] ERROR: Failed to start task for first data");
            return false;
        }
        // Give the task a moment to initialize
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Check if we have enough memory for the operation
    size_t free_psram = psramFound() ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    
    // Refuse if memory is critically low
    bool use_psram = psramFound() && size > 1024;
    size_t available = use_psram ? free_psram : free_heap;
    
    if (available < size + 4096) { // Keep 4KB buffer
        Serial.println("[DECODE] WARNING: Memory too low for enqueue - Available: " + 
                       String(available) + ", Need: " + String(size));
        stats.raw_queue_overflows++;
        return false;
    }
    
    // Create chunk in PSRAM/heap
    SPIDataChunk* chunk = new SPIDataChunk(size);
    if (!chunk->data) {
        delete chunk;
        stats.raw_queue_overflows++;
        return false;
    }
    
    // Copy data to buffer
    memcpy(chunk->data, data, size);
    
    // Try to queue (non-blocking)
    if (xQueueSend(raw_data_queue, &chunk, 0) != pdTRUE) {
        Serial.println("[DECODE] ERROR: Raw data queue full - dropping " + String(size) + " byte chunk");
        delete chunk;
        stats.raw_queue_overflows++;
        return false;
    }
    
    stats.raw_chunks_received++;
    size_t queue_depth = uxQueueMessagesWaiting(raw_data_queue);
    // Serial.println("[DECODE] Enqueued chunk: " + String(size) + " bytes, queue depth: " + String(queue_depth));
    return true;
}

inline std::vector<MediaContainer*> DecodingHandler::get_decoded_media() {
    std::vector<MediaContainer*> result;
    
    if (!initialized || !decoded_media_queue) {
        return result;
    }
    
    MediaContainer* media;
    while (xQueueReceive(decoded_media_queue, &media, 0) == pdTRUE) {
        result.push_back(media);
    }
    
    return result;
}

inline void DecodingHandler::reset_statistics() {
    memset(&stats, 0, sizeof(stats));
}

// Static task function
inline void DecodingHandler::decoding_task_function(void* parameter) {
    Serial.println("[DECODE] Static task function called with parameter=" + String((unsigned long)parameter, HEX));
    
    DecodingHandler* handler = static_cast<DecodingHandler*>(parameter);
    if (!handler) {
        Serial.println("[DECODE] ERROR: Null handler parameter in task function!");
        vTaskDelete(nullptr);
        return;
    }
    
    Serial.println("[DECODE] Calling process_raw_data_internal()...");
    handler->process_raw_data_internal();
    Serial.println("[DECODE] process_raw_data_internal() returned, deleting task");
    
    // Clear task_running flag before exiting
    handler->task_running = false;
    vTaskDelete(nullptr);
}

inline void DecodingHandler::process_raw_data_internal() {
    Serial.println("[DECODE] Processing task started");
    
    unsigned long last_cleanup = 0;
    const unsigned long CLEANUP_INTERVAL = 30000; // 30 seconds
    unsigned long loop_count = 0;
    
    try {
        while (task_running) {
            loop_count++;
            SPIDataChunk* chunk = nullptr;
            
            // Debug: Show we're alive every 100 loops when no data
            if (loop_count % 100 == 0) {
                Serial.println("[DECODE] Task alive, loop " + String(loop_count) + ", task_running=" + String(task_running));
            }
            
            // Periodic cleanup of expired transfers
            if (millis() - last_cleanup > CLEANUP_INTERVAL) {
                Serial.println("[DECODE] Starting periodic cleanup...");
                if (xSemaphoreTake(context_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    auto it = ongoing_transfers.begin();
                    unsigned long current_time = millis();
                    
                    while (it != ongoing_transfers.end()) {
                        MediaContainer* media = it->second;
                        if (!media) {
                            Serial.println("[DECODE] ERROR: Null media in ongoing_transfers!");
                            it = ongoing_transfers.erase(it);
                            continue;
                        }
                        
                        uint8_t img_id = static_cast<Image*>(media)->get_image_id();
                        
                        // Check for expired status
                        bool is_expired = media->get_status() == MediaStatus::EXPIRED;
                        
                        // Check for timeout (transfers taking longer than 30 seconds)
                        bool is_timeout = false;
                        if (transfer_start_time.count(img_id)) {
                            unsigned long elapsed = current_time - transfer_start_time[img_id];
                            is_timeout = (elapsed > 30000); // 30 second timeout
                            
                            if (is_timeout) {
                                uint8_t expected = expected_chunks.count(img_id) ? expected_chunks[img_id] : 0;
                                uint8_t received = received_chunks.count(img_id) ? received_chunks[img_id] : 0;
                                Serial.println("[DECODE] Transfer timeout for ID: " + String(img_id) + 
                                               " - Elapsed: " + String(elapsed) + "ms" +
                                               ", Chunks: " + String(received) + "/" + String(expected));
                            }
                        }
                        
                        if (is_expired || is_timeout) {
                            String cleanup_reason = is_expired ? "expired" : "timed out";
                            Serial.println("[DECODE] Cleaning up " + cleanup_reason + " transfer for ID: " + String(img_id));
                            delete media;
                            expected_chunks.erase(img_id);
                            received_chunks.erase(img_id);
                            transfer_start_time.erase(img_id);
                            it = ongoing_transfers.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    xSemaphoreGive(context_mutex);
                    last_cleanup = millis();
                    Serial.println("[DECODE] Periodic cleanup completed");
                }
            }
            // Wait for raw data (block with timeout)
            if (xQueueReceive(raw_data_queue, &chunk, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (!chunk) {
                    Serial.println("[DECODE] WARNING: Received null chunk from queue");
                    continue;
                }
                
                Serial.println("[DECODE] Processing chunk from queue, size: " + String(chunk->size));
                
                try {
                    uint32_t decode_start = millis();
                    
                    // Update statistics
                    stats.total_bytes_processed += chunk->size;
                    stats.last_chunk_size = chunk->size;
                    
                    // Debug: Inspect raw SPI data
                    Serial.println("[RAW-SPI] Processing " + String(chunk->size) + " bytes");
                    
                    // Basic data integrity check before decoding
                    if (chunk->size < 5) {
                        Serial.println("[DECODE] WARNING: Chunk too small (" + String(chunk->size) + " bytes), skipping");
                        delete chunk;
                        continue;
                    }
                    
                    if (chunk->size > 8192) {
                        Serial.println("[DECODE] WARNING: Chunk too large (" + String(chunk->size) + " bytes), skipping");
                        delete chunk;
                        continue;
                    }
            
                    // SOF marker validation
                    if (chunk->data[0] != 0x7E) {
                        Serial.println("[SOF-ERROR] Invalid SOF marker: 0x" + String(chunk->data[0], HEX) + 
                                    " (expected 0x7E)");
                        // Show first few bytes for debugging
                        String hex_dump = "[RAW-BYTES] ";
                        for (int i = 0; i < min((int)chunk->size, 8); i++) {
                            hex_dump += "0x" + String(chunk->data[i], HEX) + " ";
                        }
                        Serial.println(hex_dump);
                        stats.sof_marker_errors++;
                        delete chunk;
                        continue;
                    }
                    
                    // Show message type for debugging
                    if (chunk->size >= 2) {
                        uint8_t msg_type = chunk->data[1];
                        String msg_type_str = "UNKNOWN";
                        switch(msg_type) {
                            case 0x01: msg_type_str = "TextBatch"; break;
                            case 0x02: msg_type_str = "ImageStart"; break;
                            case 0x03: msg_type_str = "ImageChunk"; break;
                            case 0x04: msg_type_str = "ImageEnd"; break;
                            case 0x05: msg_type_str = "BacklightOn"; break;
                            case 0x06: msg_type_str = "BacklightOff"; break;
                        }
                        Serial.println("[MSG-TYPE] " + msg_type_str + " (0x" + String(msg_type, HEX) + ")");
                    }
                    
                    // Try to decode the message
                    size_t processed_offset = 0;
                    bool message_processed = false;
                    
                    while (processed_offset < chunk->size && !message_processed) {
                        const uint8_t* data_ptr = chunk->data + processed_offset;
                        size_t remaining_size = chunk->size - processed_offset;
                        
                        // Check SOF marker
                        if (remaining_size < 5 || data_ptr[0] != SOF_MARKER) {
                            if (processed_offset == 0) {
                                // First attempt failed, try sync recovery
                                Serial.println("[DECODE] Invalid SOF marker (0x" + String(data_ptr[0], HEX) + 
                                            "), attempting sync recovery");
                                stats.sof_marker_errors++;
                                
                                size_t recovery_offset = 0;
                                if (try_sync_recovery(chunk->data, chunk->size, recovery_offset)) {
                                    Serial.println("[DECODE] Sync recovery found SOF at offset " + String(recovery_offset));
                                    processed_offset = recovery_offset;
                                    stats.sync_recovery_attempts++;
                                    continue; // Try again with recovered offset
                                } else {
                                    Serial.println("[DECODE] Sync recovery failed - no valid SOF found");
                                    // Print chunk for debugging
                                    String hex_dump = "[DECODE] Chunk bytes: ";
                                    for (int i = 0; i < min((int)chunk->size, 16); i++) {
                                        hex_dump += "0x" + String(chunk->data[i], HEX) + " ";
                                    }
                                    Serial.println(hex_dump);
                                    break; // Give up on this chunk
                                }
                            } else {
                                // Already tried recovery, give up
                                break;
                            }
                        }
                        
                        // Try to decode the message at current offset
                        DProtocol::Message msg;
                        ErrorCode ec = DProtocol::decode(data_ptr, remaining_size, msg);
                        
                        if (ec == ErrorCode::SUCCESS) {
                            MediaContainer* decoded_media = decode_message(msg);
                            
                            if (decoded_media) {
                                // Debug: Check virtual function before queueing
                                Serial.println("[DECODE] Before queueing - virtual get_image_id(): " + String(decoded_media->get_image_id()) +
                                            ", Media Type: " + String(static_cast<int>(decoded_media->get_media_type())));
                                
                                // Queue the decoded media (non-blocking)
                                if (xQueueSend(decoded_media_queue, &decoded_media, 0) == pdTRUE) {
                                    stats.messages_decoded++;
                                } else {
                                    // Queue full, delete the media
                                    delete decoded_media;
                                    Serial.println("[DECODE] ERROR: Media queue full, dropping message");
                                }
                            } else {
                                stats.messages_decoded++; // Still count as decoded
                            }
                            message_processed = true;
                            
                        } else {
                            // Enhanced decode failure logging  
                            Serial.println("[DECODE-FAIL] EC: 0x" + String(static_cast<uint8_t>(ec), HEX) + 
                                        " at offset " + String(processed_offset) + 
                                        ", remaining: " + String(remaining_size) + "B");
                            
                            // Show raw bytes around failure point for diagnosis
                            if (remaining_size > 0) {
                                String hex_dump = "[FAIL-BYTES] ";
                                for (int i = 0; i < min((int)remaining_size, 12); i++) {
                                    hex_dump += "0x" + String(data_ptr[i], HEX) + " ";
                                }
                                Serial.println(hex_dump);
                            }
                            
                            // Print first few bytes for debugging only on significant failures
                            if (remaining_size >= 16 && processed_offset == 0) {
                                String hex_dump = "[DECODE] First bytes: ";
                                for (int i = 0; i < min(8, (int)remaining_size); i++) {
                                    hex_dump += "0x" + String(data_ptr[i], HEX) + " ";
                                }
                                Serial.println(hex_dump);
                            }
                            
                            stats.decode_failures++;
                            break; // Give up on this chunk after first decode failure
                        }
                    }
                    
                    // Debug: Show chunk processing result
                    if (!message_processed) {
                        Serial.println("[CHUNK-DROP] Failed to process " + String(chunk->size) + " byte chunk");
                    }

                    // Update timing statistics
                    uint32_t decode_time = millis() - decode_start;
                    if (decode_time > stats.max_decode_time_ms) {
                        stats.max_decode_time_ms = decode_time;
                    }
                    stats.avg_decode_time_ms = (stats.avg_decode_time_ms + decode_time) / 2;
                    
                    // Clean up the chunk
                    delete chunk;
                    
                    // Update queue depth statistics
                    stats.current_raw_queue_depth = uxQueueMessagesWaiting(raw_data_queue);
                    stats.current_decoded_queue_depth = uxQueueMessagesWaiting(decoded_media_queue);
                    
                } catch (...) {
                    Serial.println("[DECODE] ERROR: Exception caught during chunk processing!");
                    if (chunk) {
                        delete chunk;
                        chunk = nullptr;
                    }
                    // Continue running despite the error
                }
            } // End of xQueueReceive block
        } // End of while loop
    } catch (...) {
        Serial.println("[DECODE] ERROR: Fatal exception in processing task!");
        task_running = false;
    }
    
    Serial.println("[DECODE] Processing task ended, task_running=" + String(task_running));
}

inline bool DecodingHandler::try_sync_recovery(const uint8_t* data, size_t size, size_t& recovered_offset) {
    // Search for valid SOF marker in the data stream
    for (size_t i = 1; i < size - 5; i++) {
        if (data[i] == SOF_MARKER) {
            // Found potential SOF, check if it's followed by valid header
            if (i + 5 <= size) {
                uint8_t msg_type = data[i + 1];
                if (msg_type >= 0x01 && msg_type <= 0x0F) {
                    // Looks like a valid message type
                    uint16_t length = (data[i + 3] << 8) | data[i + 4];
                    if (i + 5 + length <= size && length < 8192) {
                        // Length seems reasonable
                        recovered_offset = i;
                        Serial.println("[DECODE] Potential valid message found at offset " + String(i) + 
                                       ", type: 0x" + String(msg_type, HEX) + ", length: " + String(length));
                        return true;
                    }
                }
            }
        }
    }
    return false;
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
            
        case DProtocol::TAG_IMAGE_END:
            result = handle(msg.payload.u.imageEnd);
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
                       String(is.totalSize) + "B, " + String(is.numChunks) + " chunks");
    }
    
    // Record expected chunks for this image
    expected_chunks[is.imgId] = is.numChunks;
    received_chunks[is.imgId] = 0;
    transfer_start_time[is.imgId] = millis();
    
    Image* img = new Image(is.imgId, fmt, res, is.totalSize, is.delayMs);
    if (img->get_status() == MediaStatus::EXPIRED) {
        Serial.println("[DECODE] ERROR: Failed to create image for ID " + String(is.imgId));
        expected_chunks.erase(is.imgId);
        received_chunks.erase(is.imgId);
        delete img;
        return nullptr;
    }
    
    // Debug: Verify image ID is set correctly
    Serial.println("[DECODE] Created Image with ID " + String(is.imgId) + 
                   ", virtual get_image_id(): " + String(img->get_image_id()) +
                   ", direct get_image_id_direct(): " + String(img->get_image_id_direct()));
    
    // Store in map and verify virtual function still works
    ongoing_transfers[is.imgId] = img;
    MediaContainer* stored = ongoing_transfers[is.imgId];
    Serial.println("[DECODE] After storing in map - virtual get_image_id(): " + String(stored->get_image_id()) +
                   ", Media Type: " + String(static_cast<int>(stored->get_media_type())));
    
    img->set_rotation(static_cast<Rotation>(is.rotation));
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
    uint8_t expected_chunk_id = received_count; // 0-based: first chunk should be ID 0
    
    // Debug: Track chunk sequence and detect gaps
    Serial.println("[CHUNK-SEQ] Image " + String(ic.imgId) + 
                   " - ChunkID: " + String(ic.chunkId) + 
                   " (expected: " + String(expected_chunk_id) + ")" +
                   ", Length: " + String(ic.length) + " bytes" +
                   ", Progress: " + String(received_count + 1) + "/" + String(expected_total));
    
    // Check for sequence gaps (chunks should be sequential starting from 0)
    if (ic.chunkId != expected_chunk_id) {
        Serial.println("[CHUNK-GAP] Image " + String(ic.imgId) + 
                       " - Expected chunk " + String(expected_chunk_id) + 
                       " but got " + String(ic.chunkId) + "!");
        Serial.println("[CHUNK-MISSING] Image " + String(ic.imgId) + 
                       " is missing chunks " + String(expected_chunk_id) + 
                       " through " + String(ic.chunkId - 1));
    }
    
    // Increment received count after processing
    received_chunks[ic.imgId]++;
    
    Image* img = static_cast<Image*>(it->second);
    if (img->get_status() == MediaStatus::EXPIRED) {
        Serial.println("[DECODE] ERROR: Cannot add chunk to expired image ID: " + String(ic.imgId));
        return nullptr;
    }
    
    // Add the chunk data
    img->add_chunk(ic.data, ic.length);
    
    return nullptr;
}

inline MediaContainer* DecodingHandler::handle(const DProtocol::ImageEnd& ie) {
    auto it = ongoing_transfers.find(ie.imgId);
    if (it == ongoing_transfers.end()) {
        Serial.println("[DECODE] ERROR: ImageEnd for unknown ID " + String(ie.imgId));
        return nullptr;
    }
    
    // Report final statistics for multi-chunk images
    uint8_t expected = expected_chunks.count(ie.imgId) ? expected_chunks[ie.imgId] : 0;
    uint8_t received = received_chunks.count(ie.imgId) ? received_chunks[ie.imgId] : 0;
    
    if (expected > 1) { // Only log for multi-chunk transfers
        unsigned long transfer_time = millis() - transfer_start_time[ie.imgId];
        Serial.println("[DECODE] Image ID " + String(ie.imgId) + " complete: " + 
                       String(received) + "/" + String(expected) + " chunks in " + 
                       String(transfer_time) + "ms");
    }
    
    MediaContainer* complete = it->second;
    ongoing_transfers.erase(it);
    expected_chunks.erase(ie.imgId);
    received_chunks.erase(ie.imgId);
    transfer_start_time.erase(ie.imgId);
    
    if (received != expected && expected > 1) {
        Serial.println("[DECODE] WARNING: Missing chunks for ID " + String(ie.imgId) + 
                       " - Expected: " + String(expected) + ", Received: " + String(received));
        
        // Mark incomplete image as expired so it gets cleaned up instead of staying in limbo
        complete->mark_expired();
        Serial.println("[DECODE] Marked incomplete image ID " + String(ie.imgId) + " as EXPIRED");
    }
    
    // Debug: Verify image ID before returning
    if (complete->get_media_type() == MediaType::IMAGE) {
        Image* img_ptr = static_cast<Image*>(complete);
        Serial.println("[DECODE] Returning Image - Constructor ID: " + String(ie.imgId) + 
                       ", Media Type: " + String(static_cast<int>(complete->get_media_type())) +
                       ", virtual get_image_id(): " + String(complete->get_image_id()) + 
                       ", direct get_image_id_direct(): " + String(img_ptr->get_image_id_direct()) +
                       ", Object addr: 0x" + String((unsigned long)complete, HEX));
    } else {
        Serial.println("[DECODE] ERROR: Media type is NOT IMAGE! Type: " + String(static_cast<int>(complete->get_media_type())));
    }
    
    return complete; // Return even if incomplete
}

} // namespace dice

#endif // DICE_DECODING_HANDLER_H
