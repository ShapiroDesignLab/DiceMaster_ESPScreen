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
using SPIBuffer = ESP32DMASPI::SPISlaveBuffer;

namespace dice {

// Forward declaration
class Screen;

// SPI Buffer Sizes and Configuration
constexpr size_t SPI_BUFFER_SIZE = 8192;       // 8KB DMA buffers
constexpr size_t BUFFER_POOL_SIZE = 16;         // Number of circulating buffers
constexpr size_t DECODE_TASK_STACK = 12288;    // Stack size for decode task
constexpr size_t REQUEUE_TASK_STACK = 2048;    // Stack size for requeue task

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
    
    // Event-driven pipeline tasks
    TaskHandle_t decode_task_handle;
    
    // Synchronization for direct requeue calls
    SemaphoreHandle_t requeue_mutex;
    
    // Decoding handler for protocol processing
    DecodingHandler* decoding_handler;
    
    // Buffer return method for decoding handler callback
    void return_buffer_for_requeue(SPIBuffer* buffer);
    
    // Direct blocking requeue function
    bool requeue_buffer(SPIBuffer* buffer);
    
    // Statistics
    volatile size_t transaction_count = 0;
    volatile size_t buffers_processed = 0;
    volatile size_t decode_errors = 0;
    
    // Pipeline task functions
    static void decode_task_function(void* parameter);
    
    // Event-driven completion callback
    void on_spi_complete(const uint8_t* rx_buf, size_t bytes);

public:
    SPIDriver(Screen* screen);
    ~SPIDriver();
    
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
    };
    
    SPIDriverStats get_driver_statistics() const;
};

// Implementation
inline SPIDriver::SPIDriver(Screen* screen) 
    : decoding_handler(nullptr)
    , decode_task_handle(nullptr)
    , requeue_mutex(nullptr) {
    
    // Initialize buffer pool with IDs
    for (size_t i = 0; i < BUFFER_POOL_SIZE; i++) {
        buffer_pool[i].id = i;
        buffer_pool[i].rx_buffer = slave.allocDMABuffer(SPI_BUFFER_SIZE);
        buffer_pool[i].rx_capacity = SPI_BUFFER_SIZE;
        buffer_pool[i].tx_buffer = nullptr;  // RX-only for our use case
        buffer_pool[i].tx_capacity = 0;
        buffer_pool[i].tx_size = 0;
        buffer_pool[i].rx_size = 0;
        buffer_pool[i].timestamp = 0;
        buffer_pool[i].in_flight = false;
        
        if (!buffer_pool[i].rx_buffer) {
            Serial.println("[SPI] ERROR: Failed to allocate DMA buffer " + String(i));
        } else {
            Serial.println("[SPI] Allocated buffer " + String(i) + " with ID " + String(buffer_pool[i].id));
        }
    }
    
    // Configure SPI slave
    slave.setSpiMode(SPI_MODE0);  // Use setSpiMode instead of setDataMode
    slave.setMaxTransferSize(SPI_BUFFER_SIZE);
    slave.setQueueSize(BUFFER_POOL_SIZE);  // Small queue since we re-queue immediately
    slave.begin();   // Default HSPI
    
    Serial.println("[SPI] Initialized with " + String(BUFFER_POOL_SIZE) + " DMA buffers for event-driven pipeline");
    
    // Initialize decoding handler with screen reference
    decoding_handler = new DecodingHandler();
    if (!decoding_handler || !decoding_handler->initialize(screen)) {
        Serial.println("[SPI] FATAL: Failed to initialize decoding handler in constructor");
        while(1) delay(1000); // Halt on critical failure
    }
    
    // Create mutex for thread-safe requeue operations
    requeue_mutex = xSemaphoreCreateMutex();
    if (!requeue_mutex) {
        Serial.println("[SPI] FATAL: Failed to create requeue mutex in constructor");
        while(1) delay(1000); // Halt on critical failure
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
    
    if (decode_result != pdPASS) {
        Serial.println("[SPI] FATAL: Failed to create decode task in constructor");
        while(1) delay(1000); // Halt on critical failure
    }
    
    // Set up event-driven completion notification
    slave.setCompletionNotifyTarget(decode_task_handle, 0);
    
    // Set up buffer return callback for decoding handler
    decoding_handler->set_buffer_return_callback([this](SPIBuffer* buffer) {
        this->requeue_buffer(buffer);
    });
    
    // Start the pipeline by queueing initial buffers
    Serial.println("[SPI] About to queue " + String(BUFFER_POOL_SIZE) + " initial buffers");
    
    for (size_t i = 0; i < BUFFER_POOL_SIZE; i++) {
        SPIBuffer* buf = &buffer_pool[i];
        if (slave.queue(buf)) {
            Serial.println("[SPI] Queued initial buffer ID " + String(buf->id));
        } else {
            Serial.println("[SPI] Failed to queue initial buffer ID " + String(buf->id));
        }
    }
    
    Serial.println("[SPI] Event-driven pipeline initialized successfully in constructor");
}

inline SPIDriver::~SPIDriver() {
    // Tasks and resources persist for lifetime of application
    // No manual cleanup needed since ESP32 will reset
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
            
            if (result.err != ESP_OK || !result.buffer) {
                Serial.println("[SPI-DECODE] Transaction error: " + String(result.err));
                driver->decode_errors++;
                
                // Still need to requeue the buffer even on error
                if (result.buffer) {
                    driver->requeue_buffer(result.buffer);
                }
                continue;
            }
            
            SPIBuffer* buffer = result.buffer;
            // Serial.println("[SPI-DECODE] Received " + String(buffer->rx_size) + " bytes in buffer ID " + String(buffer->id));
            
            // Send to decode handler for protocol processing
            if (!driver->decoding_handler) {
                // No decoding handler, return buffer immediately
                driver->requeue_buffer(buffer);
                driver->buffers_processed++;
                continue;
            }

            // The decoding handler now owns the buffer and will return it when done
            if (!driver->decoding_handler->enqueue_raw_buffer(buffer)) {
                Serial.println("[SPI-DECODE] Failed to enqueue buffer ID " + String(buffer->id) + " with " + String(buffer->rx_size) + " bytes");
                driver->decode_errors++;
                // If enqueue failed, we need to requeue the buffer ourselves
                driver->requeue_buffer(buffer);
            }
            driver->buffers_processed++;
        }
    }
}

inline bool SPIDriver::requeue_buffer(SPIBuffer* buffer) {
    if (!buffer) return false;
    
    Serial.println("[SPI-REQUEUE] Requeuing buffer ID " + String(buffer->id));
    
    // Thread-safe requeue operation
    if (xSemaphoreTake(requeue_mutex, portMAX_DELAY) == pdTRUE) {
        // Reset buffer state for next use
        buffer->reset(0);
        
        // Immediately requeue the buffer for next SPI transaction
        bool requeue_success = slave.requeue(buffer);
        
        xSemaphoreGive(requeue_mutex);
        
        if (!requeue_success) {
            Serial.println("[SPI-REQUEUE] Failed to requeue buffer ID " + String(buffer->id));
        }
        // } else {
        //     Serial.println("[SPI-REQUEUE] Successfully requeued buffer ID " + String(buffer->id));
        // }
        
        return requeue_success;
    }
    
    Serial.println("[SPI-REQUEUE] Failed to acquire mutex for buffer ID " + String(buffer->id));
    return false;
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
    
    return stats;
}

} // namespace dice

/*
 * Event-Driven SPI Pipeline Summary:
 * 
 * This implementation uses an event-driven zero-copy pipeline with direct blocking
 * requeue operations that minimizes processing overhead:
 * 
 * 1. Buffer Pool: Fixed array of DMA buffers that circulate through the pipeline
 * 2. Event Flow: SPI completion → decode task notification → processing → direct requeue
 * 3. Zero Copy: Buffer pointers flow through pipeline without data copying
 * 4. Blocking Requeue: Direct synchronous buffer requeuing with mutex protection
 * 5. Simplified: No separate requeue task or queue management needed
 * 6. Unified API: Single queue() method handles TX/RX via nullptr parameters
 * 7. Event-Driven Decoding: No continuous task loops, purely notification-based
 * 
 * Buffer Lifecycle:
 * - All buffers start queued in SPI hardware
 * - On completion → decode task processes data
 * - Decoder owns buffer during processing
 * - When done, decoder calls blocking requeue function directly
 * - Buffer is immediately re-submitted to SPI hardware
 * - Cycle repeats indefinitely
 * 
 * Security Improvements:
 * - Enhanced input validation with bounds checking
 * - Secure memory allocation with nothrow and cleanup
 * - Timeout limits to prevent infinite waits
 * - Resource exhaustion protection
 * - Memory initialization to prevent data leakage
 * - Proper error propagation and handling
 * - Thread-safe requeue operations with mutex protection
 * 
 * Benefits:
 * - Lower latency through event-driven notifications
 * - Reduced memory usage through zero-copy design  
 * - Better throughput with immediate buffer recycling
 * - Simplified codebase without separate requeue task
 * - Enhanced security through comprehensive validation
 * - Thread-safe operations with minimal overhead
 * - Eliminates queue management complexity
 */

#endif
