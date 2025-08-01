#ifndef DICE_SPI_NEW
#define DICE_SPI_NEW

#include <vector>
#include <string>
#include <map>

#include <ESP32DMASPISlave.h>
#include "media.h"
#include "protocol.h"
#include "constants.h"
#include "decoding_handler.h"

namespace dice {

// SPI Buffer Sizes and Configuration
constexpr size_t SPI_MOSI_BUFFER_SIZE = 8192;  // Increased for larger messages
constexpr size_t MAX_QUEUE_SIZE = 16;           // Maximum number of pre-queued transactions
constexpr size_t DEFAULT_QUEUE_SIZE = 1;       // Default single transaction

/**
 * Multi-Buffer SPI Driver with Performance Optimization
 * 
 * This driver implements intelligent pre-queueing to reduce transfer delays:
 * - Uses 16 DMA buffers to allow multiple transactions to be queued simultaneously
 * - Detects ImageStart messages and pre-queues based on chunk count 
 * - Reduces 20+ms delays between transfers by eliminating round-trip wait times
 * - Protocol-aware: reads chunk count from ImageStart (byte 14) to optimize batching
 */

class SPIDriver {
private:
    ESP32DMASPI::Slave slave;
    uint8_t* dma_tx_buf {nullptr};
    
    // Multi-buffer system for pre-queueing
    static constexpr size_t NUM_BUFFERS = MAX_QUEUE_SIZE;
    uint8_t* dma_rx_buffers[NUM_BUFFERS];
    bool buffer_in_use[NUM_BUFFERS];
    size_t current_queue_size;
    size_t expected_transactions;  // Number of transactions we're expecting

    // Decoding handler for asynchronous processing
    DecodingHandler* decoding_handler;

    // Simple callback counters for debugging (ISR-safe)
    volatile size_t transaction_count = 0;
    volatile bool new_data_available = false;
    
    // Timing statistics (not ISR-safe, only updated in main thread)
    unsigned long max_poll_time_ms = 0;
    unsigned long total_poll_time_ms = 0;
    size_t poll_count = 0;

    // User-defined callback for SPI transaction completion
    static void IRAM_ATTR spi_transaction_callback(spi_slave_transaction_t *trans, void *arg);
    
    // Buffer management helpers
    int get_free_buffer_index();
    void release_buffer(size_t index);
    bool is_image_start_message(const uint8_t* data, size_t size);
    uint8_t extract_chunk_count(const uint8_t* data, size_t size);
    void queue_multiple_transactions(size_t count);
    void queue_single_transaction();

public:
    SPIDriver();
    ~SPIDriver();
    
    /** Initialize SPI and start first transaction */
    bool initialize();
    
    /** Poll for completed transactions and process them (call frequently) */
    void poll_transactions();
    
    /** Get decoded media containers from the decoding handler (called at 30Hz) */
    std::vector<MediaContainer*> get_decoded_media();
    
    /** Get decoding handler statistics */
    DecodingHandler::Statistics get_decode_statistics() const;
    
    /** Get SPI transaction count */
    size_t get_transaction_count() const { return transaction_count; }
    
    /** Get SPI timing statistics */
    struct SPITimingStats {
        unsigned long max_poll_time_ms;
        unsigned long avg_poll_time_ms;
        size_t poll_count;
    };
    
    SPITimingStats get_spi_timing_stats() const {
        return {
            max_poll_time_ms,
            poll_count > 0 ? total_poll_time_ms / poll_count : 0,
            poll_count
        };
    }
};

// Implementation
inline SPIDriver::SPIDriver() : decoding_handler(nullptr), current_queue_size(0), expected_transactions(0) {
    // Initialize all DMA buffers
    for (size_t i = 0; i < NUM_BUFFERS; i++) {
        dma_rx_buffers[i] = slave.allocDMABuffer(SPI_MOSI_BUFFER_SIZE);
        buffer_in_use[i] = false;
        if (!dma_rx_buffers[i]) {
            Serial.println("[SPI] ERROR: Failed to allocate DMA buffer " + String(i));
        }
    }
    dma_tx_buf = nullptr; // No TX buffer needed since we're not replying

    slave.setDataMode(SPI_MODE0);
    slave.setMaxTransferSize(SPI_MOSI_BUFFER_SIZE);
    slave.setQueueSize(MAX_QUEUE_SIZE);  // Allow up to 16 queued transactions
    slave.begin();   // Default HSPI
    
    // Initialize decoding handler
    decoding_handler = new DecodingHandler();
    
    Serial.println("[SPI] Initialized with " + String(NUM_BUFFERS) + " DMA buffers for pre-queueing");
}

inline SPIDriver::~SPIDriver() {
    if (decoding_handler) {
        decoding_handler->shutdown();
        delete decoding_handler;
        decoding_handler = nullptr;
    }
}

// Buffer management helper methods
inline int SPIDriver::get_free_buffer_index() {
    for (size_t i = 0; i < NUM_BUFFERS; i++) {
        if (!buffer_in_use[i]) {
            return i;
        }
    }
    return -1; // No free buffers
}

inline void SPIDriver::release_buffer(size_t index) {
    if (index < NUM_BUFFERS) {
        buffer_in_use[index] = false;
    }
}

inline bool SPIDriver::is_image_start_message(const uint8_t* data, size_t size) {
    // Check if we have enough data and correct SOF marker and message type
    // Protocol: [SOF(1)] [MSG_TYPE(1)] [SEQ(1)] [LENGTH(2)] [PAYLOAD...]
    // ImageStart message type is 0x02
    if (size >= 5 && data[0] == 0x7E && data[1] == 0x02) {  // SOF_MARKER = 0x7E, ImageStart = 0x02
        return true;
    }
    return false;
}

inline uint8_t SPIDriver::extract_chunk_count(const uint8_t* data, size_t size) {
    if (size >= 13) {  // Need at least 13 bytes to access byte 12 (0-indexed)
        return data[12];  // numChunks field
    }
    return 1; // Default to 1 chunk if we can't extract
}

inline void SPIDriver::queue_multiple_transactions(size_t count) {
    Serial.println("[SPI] Pre-queueing " + String(count) + " transactions for fast transfer");
    
    size_t queued = 0;
    for (size_t i = 0; i < count && i < NUM_BUFFERS; i++) {
        int buffer_idx = get_free_buffer_index();
        if (buffer_idx >= 0) {
            buffer_in_use[buffer_idx] = true;
            slave.queue(NULL, dma_rx_buffers[buffer_idx], SPI_MOSI_BUFFER_SIZE);
            queued++;
        } else {
            Serial.println("[SPI] WARNING: No free buffers for pre-queueing, queued: " + String(queued));
            break;
        }
    }
    
    if (queued > 0) {
        slave.trigger();
        current_queue_size = queued;
        expected_transactions = count;
        Serial.println("[SPI] Successfully pre-queued " + String(queued) + " transactions");
    }
}

inline bool SPIDriver::initialize() {
    if (!decoding_handler || !decoding_handler->initialize()) {
        Serial.println("[SPI] Failed to initialize decoding handler");
        return false;
    }
    
    // Set up SPI callback (minimal ISR-safe callback)
    slave.setUserPostTransCbAndArg(spi_transaction_callback, (void*)this);
    
    // Start first transaction with first buffer
    buffer_in_use[0] = true;
    slave.queue(NULL, dma_rx_buffers[0], SPI_MOSI_BUFFER_SIZE);
    slave.trigger();
    current_queue_size = 1;
    
    Serial.println("[SPI] Initialized with callback-based processing and multi-buffer support");
    return true;
}

// Minimal ISR-safe callback function - only sets flags
inline void IRAM_ATTR SPIDriver::spi_transaction_callback(spi_slave_transaction_t *trans, void *arg) {
    // NOTE: This runs in ISR context - keep it minimal!
    SPIDriver* spi_driver = static_cast<SPIDriver*>(arg);
    
    // Increment transaction counter (atomic operation)
    spi_driver->transaction_count++;
    
    // Signal that new data is available (atomic flag)
    spi_driver->new_data_available = true;
}

// Process completed transactions (called from main loop, not ISR)
inline void SPIDriver::poll_transactions() {
    static unsigned long last_poll_time = 0;
    unsigned long poll_start = millis();
    
    // Check if new data is available (atomic read)
    if (!new_data_available) {
        return;
    }
    
    // Check if we have completed transactions
    if (slave.numTransactionsCompleted() == 0) {
        return;
    }
    
    // Process completed transactions
    const std::vector<size_t> sizes = slave.numBytesReceivedAll();
    if (sizes.empty()) {
        // Queue next transaction if we have free buffers
        queue_single_transaction();
        return;
    }
    
    unsigned long poll_gap = last_poll_time > 0 ? (poll_start - last_poll_time) : 0;
    
    // Process each completed transaction
    size_t processed_count = 0;
    unsigned long process_start = millis();
    size_t total_bytes = 0;
    size_t buffer_index = 0;
    
    for (size_t sz : sizes) {
        // Find which buffer this transaction used
        while (buffer_index < NUM_BUFFERS && !buffer_in_use[buffer_index]) {
            buffer_index++;
        }
        
        if (buffer_index >= NUM_BUFFERS) {
            Serial.println("[SPI] ERROR: No buffer found for completed transaction");
            break;
        }
        
        const uint8_t* buf = dma_rx_buffers[buffer_index];
        total_bytes += sz;
        
        if (sz > 0 && decoding_handler) {
            // Check if this is an ImageStart message for pre-queueing
            if (is_image_start_message(buf, sz)) {
                uint8_t chunk_count = extract_chunk_count(buf, sz);
                Serial.println("[SPI] Detected ImageStart with " + String(chunk_count + 1) + " total messages (1 start + " + String(chunk_count) + " chunks)");
                
                // Pre-queue additional transactions for chunk_count + 1 total messages
                // We already have 1 queued, so queue chunk_count more
                if (chunk_count > 0) {
                    queue_multiple_transactions(chunk_count);
                }
            }
            
            // Enqueue the data to decoding handler
            bool enqueue_success = decoding_handler->enqueue_raw_data(buf, sz);
            
            if (!enqueue_success) {
                Serial.println("[SPI] ERROR: Failed to enqueue " + String(sz) + " bytes");
            }
        }
        
        // Release this buffer
        release_buffer(buffer_index);
        processed_count++;
        buffer_index++;
    }
    
    // Update queue size
    current_queue_size = std::max(0, (int)current_queue_size - (int)processed_count);
    
    unsigned long process_time = millis() - process_start;
    // Log processing summary
    if (processed_count > 0) {
        Serial.println("[SPI] Processed " + String(processed_count) + " msgs, " + String(total_bytes) + "B");
    }
    
    // Queue more transactions if needed
    queue_single_transaction();
    
    // Reset the flag (atomic write)
    new_data_available = false;
    last_poll_time = poll_start;
    
    // Update timing statistics
    unsigned long total_time = millis() - poll_start;
    if (total_time > max_poll_time_ms) {
        max_poll_time_ms = total_time;
    }
    total_poll_time_ms += total_time;
    poll_count++;
}

// Helper method to queue a single transaction
inline void SPIDriver::queue_single_transaction() {
    if (slave.hasTransactionsCompletedAndAllResultsHandled() && current_queue_size == 0) {
        int buffer_idx = get_free_buffer_index();
        if (buffer_idx >= 0) {
            buffer_in_use[buffer_idx] = true;
            slave.setUserPostTransCbAndArg(spi_transaction_callback, (void*)this);
            slave.queue(NULL, dma_rx_buffers[buffer_idx], SPI_MOSI_BUFFER_SIZE);
            slave.trigger();
            current_queue_size = 1;
        } else {
            Serial.println("[SPI] WARNING: No free buffers for next transaction");
        }
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

} // namespace dice
#endif
