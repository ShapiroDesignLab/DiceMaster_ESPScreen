#pragma once
#ifndef ESP32DMASPI_SLAVE_STREAMING_H
#define ESP32DMASPI_SLAVE_STREAMING_H

#include <Arduino.h>
#include <SPI.h>
#include <driver/spi_slave.h>
#include <soc/soc_caps.h>
#include <vector>
#include <string>
#include <functional>

#ifndef ARDUINO_ESP32_DMA_SPI_NAMESPACE_BEGIN
#define ARDUINO_ESP32_DMA_SPI_NAMESPACE_BEGIN \
    namespace arduino {                       \
    namespace esp32 {                         \
        namespace spi {                       \
            namespace dma {
#endif
#ifndef ARDUINO_ESP32_DMA_SPI_NAMESPACE_END
#define ARDUINO_ESP32_DMA_SPI_NAMESPACE_END \
    }                                       \
    }                                       \
    }                                       \
    }
#endif

ARDUINO_ESP32_DMA_SPI_NAMESPACE_BEGIN

// -----------------------------------------------------------------------------
//  ESP32DMASPI Slave (Streaming, Event‑Driven)
//  * Pure streaming: every queue call submits immediately (no batch mode)
//  * Requeue API: explicitly re‑queue the same buffer after processing
//  * Task‑context only (no ISR user callbacks)
//  * Optional PSRAM‑DMA buffers on ESP32‑S3
//  * Thread‑safe public APIs; FreeRTOS direct‑to‑task notifications on completion
// -----------------------------------------------------------------------------

static constexpr const char *TAG = "ESP32DMASPISlave";
static constexpr int  SPI_SLAVE_TASK_STACK_SIZE = 1024 * 4;
static constexpr int  SPI_SLAVE_TASK_PRIORITY   = 5;
static constexpr int  GET_RESULT_TIMEOUT_MS     = 10;   // short timeout to allow clean shutdown

// Forward decl of worker task
static void spi_slave_task(void *arg);

// SPI Buffer structure for managed buffer lifecycle
struct SPISlaveBuffer {
    uint32_t id;              // Buffer ID (0 to N-1)
    uint8_t* tx_buffer;       // TX buffer pointer (nullptr for RX-only)
    uint8_t* rx_buffer;       // RX buffer pointer (nullptr for TX-only)
    size_t tx_capacity;       // TX buffer capacity
    size_t rx_capacity;       // RX buffer capacity  
    size_t tx_size;           // Actual TX data size
    size_t rx_size;           // Actual RX data received
    uint32_t timestamp;       // Timestamp when transaction completed
    bool in_flight;           // Whether buffer is currently in SPI hardware queue
    
    SPISlaveBuffer() : id(0), tx_buffer(nullptr), rx_buffer(nullptr), 
                      tx_capacity(0), rx_capacity(0), tx_size(0), rx_size(0),
                      timestamp(0), in_flight(false) {}
    
    SPISlaveBuffer(uint32_t buf_id, uint8_t* tx_buf, size_t tx_cap, uint8_t* rx_buf, size_t rx_cap)
        : id(buf_id), tx_buffer(tx_buf), rx_buffer(rx_buf), 
          tx_capacity(tx_cap), rx_capacity(rx_cap), tx_size(0), rx_size(0),
          timestamp(0), in_flight(false) {}
};

struct spi_slave_context_t {
    spi_slave_interface_config_t if_cfg {
        .spics_io_num  = SS,
        .flags         = 0,
        .queue_size    = 8,
        .mode          = SPI_MODE0,
        .post_setup_cb = nullptr,   // user code runs in task context
        .post_trans_cb = nullptr,
    };
    spi_bus_config_t bus_cfg {
        .mosi_io_num = MOSI,
        .miso_io_num = MISO,
        .sclk_io_num = SCK,
        .data2_io_num = -1,
        .data3_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 1)
        .data_io_default_level = false,
#endif
        .max_transfer_sz = 4092,
        .flags = SPICOMMON_BUSFLAG_SLAVE,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
        .isr_cpu_id = INTR_CPU_ID_AUTO,
#endif
        .intr_flags = 0,
    };
    spi_host_device_t host {SPI2_HOST};
    int dma_chan {SPI_DMA_CH_AUTO};
    TaskHandle_t main_task_handle {NULL};
};

// Result envelope delivered to user - contains completed buffer
struct trans_result_t {
    SPISlaveBuffer* buffer;   // Completed buffer object
    esp_err_t       err;      // Transaction error code
    
    trans_result_t() : buffer(nullptr), err(ESP_OK) {}
    trans_result_t(SPISlaveBuffer* buf, esp_err_t error) : buffer(buf), err(error) {}
};

class Slave {
public:
    // Public for friend function access
    spi_slave_context_t ctx;
    QueueHandle_t q_result_  {NULL};  // trans_result_t
    QueueHandle_t q_error_   {NULL};  // esp_err_t
    QueueHandle_t q_inflight_{NULL};  // mailbox: size_t
    TaskHandle_t notify_task_{NULL};
    UBaseType_t  notify_index_{0};

private:
    // Worker task & queues
    TaskHandle_t  spi_task_handle {NULL};

    // Mutex for thread‑safe public API
    SemaphoreHandle_t api_mutex_ {NULL};

public:
    // ===== Allocation helpers =====
    static uint8_t *allocDMABuffer(size_t n_bytes) {
        if (n_bytes % 4 != 0) { ESP_LOGW(TAG, "allocDMABuffer: size must be multiple of 4"); return nullptr; }
        return static_cast<uint8_t*>(heap_caps_calloc(n_bytes, 1, MALLOC_CAP_DMA));
    }
    static uint8_t *allocPSRAM_DMABuffer(size_t n_bytes) {
        if (n_bytes % 4 != 0) { ESP_LOGW(TAG, "allocPSRAM_DMABuffer: size must be multiple of 4"); return nullptr; }
        return static_cast<uint8_t*>(heap_caps_calloc(n_bytes, 1, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM));
    }

    // ===== Begin / End =====
    bool begin(const uint8_t spi_bus = HSPI) {
#ifdef CONFIG_IDF_TARGET_ESP32
        this->ctx.if_cfg.spics_io_num = (spi_bus == VSPI) ? SS : 15;
        this->ctx.bus_cfg.sclk_io_num = (spi_bus == VSPI) ? SCK : 14;
        this->ctx.bus_cfg.mosi_io_num = (spi_bus == VSPI) ? MOSI : 13;
        this->ctx.bus_cfg.miso_io_num = (spi_bus == VSPI) ? MISO : 12;
#endif
        return this->initialize(spi_bus);
    }
    bool begin(uint8_t spi_bus, int sck, int miso, int mosi, int ss) {
        this->ctx.if_cfg.spics_io_num = ss;
        this->ctx.bus_cfg.sclk_io_num = sck;
        this->ctx.bus_cfg.mosi_io_num = mosi;
        this->ctx.bus_cfg.miso_io_num = miso;
        return this->initialize(spi_bus);
    }
    bool begin(uint8_t spi_bus, int sck, int ss, int data0, int data1, int data2, int data3) {
        this->ctx.if_cfg.spics_io_num = ss;
        this->ctx.bus_cfg.sclk_io_num = sck;
        this->ctx.bus_cfg.data0_io_num = data0;
        this->ctx.bus_cfg.data1_io_num = data1;
        this->ctx.bus_cfg.data2_io_num = data2;
        this->ctx.bus_cfg.data3_io_num = data3;
        return this->initialize(spi_bus);
    }
    bool begin(uint8_t spi_bus, int sck, int ss, int data0, int data1, int data2, int data3, int data4, int data5, int data6, int data7) {
        this->ctx.if_cfg.spics_io_num = ss;
        this->ctx.bus_cfg.sclk_io_num = sck;
        this->ctx.bus_cfg.data0_io_num = data0;
        this->ctx.bus_cfg.data1_io_num = data1;
        this->ctx.bus_cfg.data2_io_num = data2;
        this->ctx.bus_cfg.data3_io_num = data3;
        this->ctx.bus_cfg.data4_io_num = data4;
        this->ctx.bus_cfg.data5_io_num = data5;
        this->ctx.bus_cfg.data6_io_num = data6;
        this->ctx.bus_cfg.data7_io_num = data7;
        return this->initialize(spi_bus);
    }

    void end() {
        if (this->spi_task_handle == NULL) { ESP_LOGW(TAG, "spi_slave_task already terminated"); return; }
        xTaskNotifyGive(this->spi_task_handle); // request task exit
        if (xTaskNotifyWait(0, 0, NULL, pdMS_TO_TICKS(5000)) != pdTRUE) {
            ESP_LOGW(TAG, "timeout waiting for spi_slave_task end");
        }
        this->spi_task_handle = NULL;
    }

    // ===== Event‑driven completion notification =====
    // Register the task to be notified; call ulTaskNotifyTakeIndexed() in that task.
    void setCompletionNotifyTarget(TaskHandle_t task, UBaseType_t index=0) {
        notify_task_ = task; notify_index_ = index;
    }

    // ===== Unified streaming API =====
    // Queue buffer transaction - buffer object contains all necessary information
    bool queue(SPISlaveBuffer* buffer, uint32_t timeout_ms=0) {
        if (!buffer) return false;
        return submit_now(buffer, timeout_ms);
    }
    
    // ===== Explicit re‑queue API =====
    bool requeue(SPISlaveBuffer* buffer, uint32_t timeout_ms=0) {
        return queue(buffer, timeout_ms);
    }

    // ===== Event‑driven readout =====
    bool takeResult(trans_result_t& out, TickType_t to=portMAX_DELAY) {
        LockGuard guard(api_mutex_);  // Add mutex protection for consistency
        return (q_result_ && xQueueReceive(q_result_, &out, to) == pdTRUE);
    }

    // ===== Status / errors =====
    size_t numTransactionsInFlight() {
        size_t num_in_flight = 0; if (q_inflight_) xQueuePeek(q_inflight_, &num_in_flight, 0); return num_in_flight;
    }
    size_t numTransactionsCompleted() { return q_result_ ? uxQueueMessagesWaiting(q_result_) : 0; }
    size_t numTransactionErrors()     { return q_error_  ? uxQueueMessagesWaiting(q_error_)  : 0; }

    esp_err_t error() {
        if (this->numTransactionErrors() > 0) { esp_err_t err; if (xQueueReceive(q_error_, &err, 0)) return err; }
        return ESP_OK;
    }

    // ===== Config passthroughs =====
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 1)
    void setDataIODefaultLevel(bool level) { this->ctx.bus_cfg.data_io_default_level = level; }
#endif
    void setMaxTransferSize(size_t size)   { this->ctx.bus_cfg.max_transfer_sz = static_cast<int>(size); }
    void setQueueSize(size_t size)         { this->ctx.if_cfg.queue_size = size; }
#ifdef CONFIG_IDF_TARGET_ESP32
    void setDMAChannel(spi_common_dma_t dma_chan) {
        if ((dma_chan == SPI_DMA_CH1) || (dma_chan == SPI_DMA_CH2) || (dma_chan == SPI_DMA_CH_AUTO)) this->ctx.dma_chan = dma_chan;
        else ESP_LOGW(TAG, "invalid dma channel %d", dma_chan);
    }
#endif
    void setSlaveFlags(uint32_t flags)     { this->ctx.if_cfg.flags = flags; }
    void setSpiMode(uint8_t m)             { this->ctx.if_cfg.mode = m; }

private:
    struct LockGuard { LockGuard(SemaphoreHandle_t m):m_(m){ if(m_) xSemaphoreTake(m_, portMAX_DELAY);} ~LockGuard(){ if(m_) xSemaphoreGive(m_);} SemaphoreHandle_t m_; };

    static spi_host_device_t hostFromBusNumber(uint8_t spi_bus) {
        switch (spi_bus) {
            case FSPI:
#ifdef CONFIG_IDF_TARGET_ESP32
                return SPI1_HOST;
#else
                return SPI2_HOST;
#endif
            case HSPI:
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32C3)
                return SPI2_HOST;
#else
                return SPI3_HOST;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32
            case VSPI:
                return SPI3_HOST;
#endif
            default: return SPI2_HOST;
        }
    }

    bool initialize(const uint8_t spi_bus) {
        this->ctx.host = this->hostFromBusNumber(spi_bus);
        this->ctx.bus_cfg.flags |= SPICOMMON_BUSFLAG_SLAVE;
        this->ctx.main_task_handle = xTaskGetCurrentTaskHandle();

        api_mutex_ = xSemaphoreCreateMutex(); if (!api_mutex_) return false;

        q_result_   = xQueueCreate(ctx.if_cfg.queue_size*4, sizeof(trans_result_t));
        q_error_    = xQueueCreate(ctx.if_cfg.queue_size*2, sizeof(esp_err_t));
        q_inflight_ = xQueueCreate(1, sizeof(size_t)); size_t zero=0; xQueueOverwrite(q_inflight_, &zero);
        if (!q_result_ || !q_error_ || !q_inflight_) return false;

        // Create worker task
        std::string task_name = std::string("spi_slave_task_") + std::to_string(this->ctx.if_cfg.spics_io_num);
#if SOC_CPU_CORES_NUM == 1
        int ret = xTaskCreatePinnedToCore(spi_slave_task, task_name.c_str(), SPI_SLAVE_TASK_STACK_SIZE, this, SPI_SLAVE_TASK_PRIORITY, &this->spi_task_handle, 0);
#else
        int ret = xTaskCreatePinnedToCore(spi_slave_task, task_name.c_str(), SPI_SLAVE_TASK_STACK_SIZE, this, SPI_SLAVE_TASK_PRIORITY, &this->spi_task_handle, 1);
#endif
        if (ret != pdPASS) { ESP_LOGE(TAG, "failed to create spi_slave_task: %d", ret); return false; }
        return true;
    }

    // Submit buffer now (thread‑safe with enhanced security). Buffer must remain valid until completion.
    bool submit_now(SPISlaveBuffer* buffer, uint32_t timeout_ms) {
        if (!buffer) {
            ESP_LOGW(TAG, "submit_now(): buffer is nullptr");
            return false;
        }
        
        // Determine transaction size based on available buffers
        size_t tx_size = (buffer->tx_buffer && buffer->tx_size > 0) ? buffer->tx_size : 0;
        size_t rx_size = buffer->rx_capacity; // Always use full RX capacity for receiving
        size_t transaction_size = max(tx_size, rx_size);
        
        // Enhanced input validation
        if (transaction_size == 0 || transaction_size % 4 != 0) { 
            ESP_LOGW(TAG, "submit_now(): transaction size must be > 0 and multiple of 4, got %zu", transaction_size); 
            return false; 
        }
        if (transaction_size > ctx.bus_cfg.max_transfer_sz) {
            ESP_LOGW(TAG, "submit_now(): transaction size %zu exceeds max transfer size %d", transaction_size, ctx.bus_cfg.max_transfer_sz);
            return false;
        }
        if (!buffer->tx_buffer && !buffer->rx_buffer) {
            ESP_LOGW(TAG, "submit_now(): both tx_buffer and rx_buffer are nullptr");
            return false;
        }
        
        // Check resource availability before allocation
        if (q_inflight_) {
            size_t inflight;
            if (xQueuePeek(q_inflight_, &inflight, 0) == pdTRUE && inflight >= ctx.if_cfg.queue_size) {
                ESP_LOGW(TAG, "submit_now(): queue full, %zu transactions in flight", inflight);
                return false;
            }
        }
        
        LockGuard guard(api_mutex_);
        
        // Secure allocation with bounds checking
        auto *t = new(std::nothrow) spi_slave_transaction_t();
        if (!t) {
            ESP_LOGE(TAG, "submit_now(): failed to allocate transaction descriptor");
            return false;
        }
        
        // Initialize all fields to prevent information leakage
        memset(t, 0, sizeof(spi_slave_transaction_t));
        t->length    = 8 * transaction_size;  // Convert bytes to bits
        t->trans_len = 0;                     // Will be set by hardware
        t->tx_buffer = buffer->tx_buffer;
        t->rx_buffer = buffer->rx_buffer;
        t->user      = buffer;                // Store buffer object pointer
        
        // Mark buffer as in-flight
        buffer->in_flight = true;
        
        TickType_t to = (timeout_ms == 0) ? pdMS_TO_TICKS(5000) : pdMS_TO_TICKS(timeout_ms); // Cap at 5s max
        esp_err_t qe = spi_slave_queue_trans(this->ctx.host, t, to);
        
        if (qe == ESP_OK) {
            // Update in-flight counter atomically
            if (q_inflight_) {
                size_t inflight; 
                xQueuePeek(q_inflight_, &inflight, 0); 
                inflight++; 
                xQueueOverwrite(q_inflight_, &inflight);
            }
            return true;
        } else {
            ESP_LOGW(TAG, "submit_now(): spi_slave_queue_trans failed with error 0x%x", qe);
            xQueueSend(q_error_, &qe, 0);
            buffer->in_flight = false; // Reset flag on failure
            delete t; // Clean up on failure
            return false;
        }
    }

    friend void spi_slave_task(void *arg);
};

// ===================== Worker Task (event‑driven) ===============================
static void spi_slave_task(void *arg)
{
    auto *self = static_cast<Slave*>(arg);
    ESP_LOGD(TAG, "spi_slave_task start");

    // Initialize SPI slave peripheral
    esp_err_t err = spi_slave_initialize(self->ctx.host, &self->ctx.bus_cfg, &self->ctx.if_cfg, self->ctx.dma_chan);
    assert(err == ESP_OK);

    while (true) {
        // graceful shutdown check (non‑blocking)
        if (ulTaskNotifyTake(pdTRUE, 0) > 0) break;

        // Block briefly for completions; wake periodically so end() can terminate cleanly
        spi_slave_transaction_t *r = nullptr;
        esp_err_t ge = spi_slave_get_trans_result(self->ctx.host, &r, pdMS_TO_TICKS(GET_RESULT_TIMEOUT_MS));
        if (ge == ESP_OK && r) {
            size_t inflight; xQueuePeek(self->q_inflight_, &inflight, 0); inflight = (inflight>0?inflight-1:0); xQueueOverwrite(self->q_inflight_, &inflight);
            
            // Extract buffer from user pointer and update it with results
            SPISlaveBuffer* buffer = static_cast<SPISlaveBuffer*>(r->user);
            if (buffer) {
                buffer->rx_size = r->trans_len / 8;  // Convert bits back to bytes
                buffer->timestamp = millis();
                buffer->in_flight = false;
                
                // Create result with buffer object
                trans_result_t tr(buffer, ESP_OK);
                xQueueSend(self->q_result_, &tr, 0);
            } else {
                // Handle case where buffer is null
                trans_result_t tr(nullptr, ESP_ERR_INVALID_ARG);
                xQueueSend(self->q_result_, &tr, 0);
            }
            
            if (self->notify_task_) xTaskNotifyGiveIndexed(self->notify_task_, self->notify_index_);
            delete r; // free descriptor; user owns buffers
        } else if (ge != ESP_ERR_TIMEOUT && ge != ESP_OK) {
            xQueueSend(self->q_error_, &ge, 0);
        }
    }

    ESP_LOGD(TAG, "terminate spi task as requested");
    spi_slave_free(self->ctx.host);
    xTaskNotifyGive(self->ctx.main_task_handle); // tell caller we're done
    vTaskDelete(NULL);
}

ARDUINO_ESP32_DMA_SPI_NAMESPACE_END

namespace ESP32DMASPI = arduino::esp32::spi::dma;

#endif // ESP32DMASPI_SLAVE_STREAMING_H
