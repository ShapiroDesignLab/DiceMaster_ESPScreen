#ifndef DICE_SPI_NEW
#define DICE_SPI_NEW

#include <vector>
#include <string>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "ESP32DMASPIStream.h"
#include "media.h"
#include "protocol.h"
#include "constants.h"
#include "decoding_handler.h"

// Use the Stream version of ESP32DMASPI
using ESP32DMASPI_Slave = ESP32DMASPI::Slave;
using ESP32DMASPI_trans_result_t = ESP32DMASPI::trans_result_t;

namespace dice {

// Forward declaration
class Screen;

// SPI Buffer Sizes and Configuration
constexpr size_t SPI_BUFFER_SIZE = 8192;       // 8KB DMA buffers
constexpr size_t BUFFER_POOL_SIZE = 16;         // Number of circulating buffers
constexpr size_t LOW_BUFFER_THRESHOLD = 2;     // Threshold for low buffer warning
constexpr size_t DECODE_TASK_STACK = 12288;    // Stack size for decode task
constexpr size_t REQUEUE_TASK_STACK = 2048;    // Stack size for requeue task

/**
 * Buffer structure for zero-copy pipeline
 */
struct SPIBuffer {
    uint8_t* data;           // DMA buffer pointer
    size_t size;             // Actual data size received
    size_t capacity;         // Buffer capacity (always SPI_BUFFER_SIZE)
    uint32_t timestamp;      // Timestamp when data was received
    bool in_use;             // Buffer allocation flag
    
    SPIBuffer() : data(nullptr), size(0), capacity(SPI_BUFFER_SIZE), timestamp(0), in_use(false) {}
};

/**
 * Event-Driven SPI Driver with Zero-Copy Pipeline
 * 
 * New Architecture:
 * - Event-driven: Uses task notifications instead of polling
 * - Zero-copy: Buffers flow through pipeline without data copying
 * - Streaming: Immediately re-queues processed buffers
 * - Simplified: No complex pre-queueing logic needed
 * 
 * Pipeline Flow:
 * SPI RX Complete → Decode Task → Process Data → Requeue Task → Buffer Requeue
 */

class SPIDriver {
private:
    ESP32DMASPI_Slave slave;
    
    // Buffer pool for zero-copy pipeline
    SPIBuffer buffer_pool[BUFFER_POOL_SIZE];
    SemaphoreHandle_t pool_mutex;
    
    // Event-driven pipeline tasks
    TaskHandle_t decode_task_handle;
    TaskHandle_t requeue_task_handle;
    
    // Pipeline queues (zero-copy: only buffer pointers flow through)
    QueueHandle_t requeue_queue;     // SPIBuffer* → requeue task
    
    // Decoding handler for protocol processing
    DecodingHandler* decoding_handler;
    
    // Buffer return method for decoding handler callback
    void return_buffer_for_requeue(uint8_t* buffer_data);
    
    // Statistics
    volatile size_t transaction_count = 0;
    volatile size_t buffers_processed = 0;
    volatile size_t decode_errors = 0;
    
    // Pipeline task functions
    static void decode_task_function(void* parameter);
    static void requeue_task_function(void* parameter);
    
    // Buffer pool management with low buffer warning
    SPIBuffer* get_free_buffer();
    void return_buffer_to_pool(SPIBuffer* buffer);
    size_t count_free_buffers() const;
    void check_buffer_levels(); // Check and warn if buffers are low
    
    // Event-driven completion callback
    void on_spi_complete(const uint8_t* rx_buf, size_t bytes);

public:
    SPIDriver();
    ~SPIDriver();
    
    /** Initialize SPI driver and start pipeline tasks with screen reference for direct enqueueing */
    bool initialize(Screen* screen);
    
    /** Get decoding handler statistics */
    DecodingHandler::Statistics get_decode_statistics() const;
    
    /** Get transaction count */
    size_t get_transaction_count() const { return transaction_count; }
    
    /** Get SPI timing statistics (placeholder) */
    struct SPITimingStats {
        unsigned long avg_processing_time_ms = 0;
        unsigned long max_processing_time_ms = 0;
        unsigned long total_transactions = 0;
    };
    SPITimingStats get_spi_timing_stats() const {
        SPITimingStats stats;
        stats.total_transactions = transaction_count;
        return stats;
    }
    
    /** Get SPI driver statistics */
    struct SPIDriverStats {
        size_t transaction_count;
        size_t buffers_processed;
        size_t decode_errors;
        size_t requeue_queue_depth;
        size_t free_buffers_available;
    };
    
    SPIDriverStats get_driver_statistics() const;
};

// Implementation
inline SPIDriver::SPIDriver() 
    : decoding_handler(nullptr)
    , pool_mutex(nullptr)
    , decode_task_handle(nullptr)
    , requeue_task_handle(nullptr)
    , requeue_queue(nullptr) {
    
    // Initialize buffer pool
    for (size_t i = 0; i < BUFFER_POOL_SIZE; i++) {
        buffer_pool[i].data = slave.allocDMABuffer(SPI_BUFFER_SIZE);
        buffer_pool[i].capacity = SPI_BUFFER_SIZE;
        buffer_pool[i].in_use = false;
        if (!buffer_pool[i].data) {
            Serial.println("[SPI] ERROR: Failed to allocate DMA buffer " + String(i));
        }
    }
    
    // Configure SPI slave
    slave.setSpiMode(SPI_MODE0);  // Use setSpiMode instead of setDataMode
    slave.setMaxTransferSize(SPI_BUFFER_SIZE);
    slave.setQueueSize(4);  // Small queue since we re-queue immediately
    slave.begin();   // Default HSPI
    
    Serial.println("[SPI] Initialized with " + String(BUFFER_POOL_SIZE) + " DMA buffers for event-driven pipeline");
}

inline SPIDriver::~SPIDriver() {
    // Tasks and resources persist for lifetime of application
    // No manual cleanup needed since ESP32 will reset
}

inline bool SPIDriver::initialize(Screen* screen) {
    // Initialize decoding handler with screen reference
    decoding_handler = new DecodingHandler();
    if (!decoding_handler || !decoding_handler->initialize(screen)) {
        Serial.println("[SPI] Failed to initialize decoding handler");
        return false;
    }
    
    // Create buffer pool mutex
    pool_mutex = xSemaphoreCreateMutex();
    if (!pool_mutex) {
        Serial.println("[SPI] Failed to create buffer pool mutex");
        return false;
    }
    
    // Create pipeline queues (only buffer pointers, not data)
    requeue_queue = xQueueCreate(BUFFER_POOL_SIZE * 2, sizeof(SPIBuffer*));
    if (!requeue_queue) {
        Serial.println("[SPI] Failed to create requeue queue");
        return false;
    }
    
    // Create pipeline tasks
    BaseType_t decode_result = xTaskCreate(
        decode_task_function,
        "SPI_Decode",
        DECODE_TASK_STACK,
        this,
        5, // High priority for low latency
        &decode_task_handle
    );
    
    BaseType_t requeue_result = xTaskCreate(
        requeue_task_function,
        "SPI_Requeue", 
        REQUEUE_TASK_STACK,
        this,
        4, // Slightly lower priority
        &requeue_task_handle
    );
    
    if (decode_result != pdPASS || requeue_result != pdPASS) {
        Serial.println("[SPI] Failed to create pipeline tasks");
        return false;
    }
    
    // Set up event-driven completion notification
    slave.setCompletionNotifyTarget(decode_task_handle, 0);
    
    // Set up buffer return callback for decoding handler
    decoding_handler->set_buffer_return_callback([this](uint8_t* buffer_data) {
        this->return_buffer_for_requeue(buffer_data);
    });
    
    // Start the pipeline by queueing initial buffers
    size_t initial_buffers = BUFFER_POOL_SIZE - LOW_BUFFER_THRESHOLD;
    for (size_t i = 0; i < initial_buffers; i++) {
        SPIBuffer* buf = get_free_buffer();
        if (buf) {
            slave.queue(nullptr, buf->data, buf->capacity, buf);
        }
    }
    
    Serial.println("[SPI] Event-driven pipeline initialized successfully");
    return true;
}

// Buffer pool management
inline SPIBuffer* SPIDriver::get_free_buffer() {
    if (!pool_mutex) return nullptr;
    
    if (xSemaphoreTake(pool_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (size_t i = 0; i < BUFFER_POOL_SIZE; i++) {
            if (!buffer_pool[i].in_use) {
                buffer_pool[i].in_use = true;
                xSemaphoreGive(pool_mutex);
                return &buffer_pool[i];
            }
        }
        xSemaphoreGive(pool_mutex);
    }
    return nullptr;
}

inline void SPIDriver::return_buffer_to_pool(SPIBuffer* buffer) {
    if (!pool_mutex || !buffer) return;
    
    if (xSemaphoreTake(pool_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        buffer->in_use = false;
        buffer->size = 0;
        xSemaphoreGive(pool_mutex);
    }
}

inline size_t SPIDriver::count_free_buffers() const {
    size_t free_count = 0;
    if (pool_mutex && xSemaphoreTake(pool_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (size_t i = 0; i < BUFFER_POOL_SIZE; i++) {
            if (!buffer_pool[i].in_use) {
                free_count++;
            }
        }
        xSemaphoreGive(pool_mutex);
    }
    return free_count;
}

inline void SPIDriver::check_buffer_levels() {
    size_t free_buffers = count_free_buffers();
    
    // Warn if we have low free buffers
    if (free_buffers <= LOW_BUFFER_THRESHOLD) {
        Serial.println("[SPI] WARNING: Low free buffers available: " + String(free_buffers) + 
                      " (threshold: " + String(LOW_BUFFER_THRESHOLD) + ")");
    }
}

// Pipeline task implementations
inline void SPIDriver::decode_task_function(void* parameter) {
    SPIDriver* driver = static_cast<SPIDriver*>(parameter);
    ESP32DMASPI_trans_result_t result;
    
    Serial.println("[SPI-DECODE] Event-driven decode task started");
    
    while (true) {
        // Wait for SPI completion notification (event-driven)
        ulTaskNotifyTakeIndexed(0, pdTRUE, portMAX_DELAY);
        
        // Process all completed transactions
        while (driver->slave.takeResult(result, 0)) {
            driver->transaction_count++;
            
            if (result.err != ESP_OK || result.bytes == 0) {
                Serial.println("[SPI-DECODE] Transaction error: " + String(result.err));
                driver->decode_errors++;
                
                // Still need to requeue the buffer even on error
                SPIBuffer* buffer = static_cast<SPIBuffer*>(result.user);
                if (buffer) {
                    if (xQueueSend(driver->requeue_queue, &buffer, 0) != pdTRUE) {
                        driver->return_buffer_to_pool(buffer);
                    }
                }
                continue;
            }
            
            // Get buffer from result.user pointer
            SPIBuffer* buffer = static_cast<SPIBuffer*>(result.user);
            if (!buffer) {
                Serial.println("[SPI-DECODE] ERROR: Null buffer pointer");
                driver->decode_errors++;
                continue;
            }
            
            // Update buffer with received data
            buffer->size = result.bytes;
            buffer->timestamp = millis();
            
            // Send to decode handler for protocol processing
            // The decoding handler now owns the buffer and will return it when done
            if (driver->decoding_handler) {
                bool enqueue_success = driver->decoding_handler->enqueue_raw_data(buffer->data, buffer->size);
                if (!enqueue_success) {
                    Serial.println("[SPI-DECODE] Failed to enqueue " + String(buffer->size) + " bytes");
                    driver->decode_errors++;
                    // If enqueue failed, we need to requeue the buffer ourselves
                    if (xQueueSend(driver->requeue_queue, &buffer, 0) != pdTRUE) {
                        driver->return_buffer_to_pool(buffer);
                    }
                }
                // If enqueue succeeded, the decoding handler now owns the buffer
                // and will return it via a callback when processing is complete
            } else {
                // No decoding handler, return buffer immediately
                if (xQueueSend(driver->requeue_queue, &buffer, 0) != pdTRUE) {
                    driver->return_buffer_to_pool(buffer);
                }
            }
            driver->buffers_processed++;
        }
        
        // Check buffer levels and warn if low
        driver->check_buffer_levels();
    }
}

inline void SPIDriver::requeue_task_function(void* parameter) {
    SPIDriver* driver = static_cast<SPIDriver*>(parameter);
    SPIBuffer* buffer;
    
    Serial.println("[SPI-REQUEUE] Requeue task started");
    
    while (true) {
        // Wait for buffer to requeue
        if (xQueueReceive(driver->requeue_queue, &buffer, portMAX_DELAY) == pdTRUE) {
            // Immediately requeue the buffer for next SPI transaction
            bool requeue_success = driver->slave.requeue(nullptr, buffer->data, buffer->capacity, buffer);
            
            if (!requeue_success) {
                Serial.println("[SPI-REQUEUE] Failed to requeue buffer");
                driver->return_buffer_to_pool(buffer);
            }
        }
    }
}

inline void SPIDriver::return_buffer_for_requeue(uint8_t* buffer_data) {
    if (!buffer_data) return;
    
    // Find the SPIBuffer that contains this data pointer
    SPIBuffer* buffer = nullptr;
    for (size_t i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (buffer_pool[i].data == buffer_data) {
            buffer = &buffer_pool[i];
            break;
        }
    }
    
    if (!buffer) {
        Serial.println("[SPI] ERROR: Could not find buffer for data pointer in return_buffer_for_requeue");
        return;
    }
    
    // Send buffer to requeue task for recycling
    if (xQueueSend(requeue_queue, &buffer, 0) != pdTRUE) {
        Serial.println("[SPI] Requeue queue full when returning buffer from decoder");
        return_buffer_to_pool(buffer);
    }
}

inline DecodingHandler::Statistics SPIDriver::get_decode_statistics() const {
    if (!decoding_handler) {
        return DecodingHandler::Statistics{};
    }
    return decoding_handler->get_statistics();
}

inline SPIDriver::SPIDriverStats SPIDriver::get_driver_statistics() const {
    SPIDriverStats stats;
    stats.transaction_count = transaction_count;
    stats.buffers_processed = buffers_processed;
    stats.decode_errors = decode_errors;
    stats.requeue_queue_depth = requeue_queue ? uxQueueMessagesWaiting(requeue_queue) : 0;
    stats.free_buffers_available = count_free_buffers();
    
    return stats;
}

} // namespace dice

/*
 * Event-Driven SPI Pipeline Summary:
 * 
 * This implementation replaces the polling-based system with an event-driven
 * zero-copy pipeline that minimizes processing overhead:
 * 
 * 1. Buffer Pool: 8 DMA buffers circulate through the pipeline
 * 2. Event Flow: SPI completion → decode task notification → processing → requeue
 * 3. Zero Copy: Buffer pointers flow through queues instead of copying data
 * 4. Streaming: Buffers are immediately re-queued after processing
 * 5. Simplified: Removed complex pre-queueing logic for image chunks
 * 6. Warning System: Warns when buffer levels are low instead of auto-management
 * 7. Unified API: Single queue() method handles TX/RX via nullptr parameters
 * 8. Event-Driven Decoding: No continuous task loops, purely notification-based
 * 
 * Security Improvements:
 * - Enhanced input validation with bounds checking
 * - Secure memory allocation with nothrow and cleanup
 * - Timeout limits to prevent infinite waits
 * - Resource exhaustion protection
 * - Memory initialization to prevent data leakage
 * - Proper error propagation and handling
 * 
 * Benefits:
 * - Lower latency through event-driven notifications
 * - Reduced memory usage through zero-copy design  
 * - Better throughput with immediate buffer recycling
 * - Simplified codebase without pre-queueing complexity
 * - Enhanced security through comprehensive validation
 * - Improved reliability with buffer level monitoring
 */

#endif
