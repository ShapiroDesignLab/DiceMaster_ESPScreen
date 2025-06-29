#ifndef DICE_SPI
#define DICE_SPI

#include <vector>
#include <string>
#include <map>

#include <ESP32DMASPISlave.h>
#include "media.h"
#include "protocol.h"

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
    std::map<uint8_t, MediaContainer*> ongoing_transfers;

    // ───────── Helpers to emit ACK / ERROR packets (using protocol.h) ────────
    void sendAck(uint8_t msgId, ErrorCode code = ErrorCode::SUCCESS)
    {
        DProtocol::Ack ack{code};
        // TODO: Implement encode function or use alternative approach
        // auto pkt = DProtocol::encode(ack, msgId);   // returns std::vector<uint8_t>
        // slave.queue(pkt.data(), nullptr, pkt.size());
    }
    void sendError(uint8_t msgId, ErrorCode code, const char* txt)
    {
        DProtocol::Error err;
        err.code = code;
        err.len = strlen(txt);
        strncpy(err.text, txt, sizeof(err.text) - 1);
        err.text[sizeof(err.text) - 1] = '\0';
        // TODO: Implement encode function or use alternative approach
        // auto pkt = DProtocol::encode(err, msgId);
        // slave.queue(pkt.data(), nullptr, pkt.size());
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
    void queue_cmd_msgs() { queueTransaction(); }
    
    /** Legacy API compatibility - now calls poll */
    std::vector<MediaContainer*> process_msgs() { return poll(); }
    
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


// ───────────────────────── queue-/poll helpers ────────────────────────
inline std::vector<MediaContainer*> SPIDriver::poll()
{
    std::vector<MediaContainer*> ready;

    if (!slave.hasTransactionsCompletedAndAllResultsReady(QUEUE_SIZE))
        return ready;

    const std::vector<size_t> sizes = slave.numBytesReceivedAll();
    size_t offset = 0;

    for (size_t sz : sizes)
    {
        const uint8_t* buf = dma_rx_buf + offset;
        offset += sz;

        DProtocol::Message msg;
        ErrorCode ec = DProtocol::decode(buf, sz, msg);

        if (ec != ErrorCode::SUCCESS)            // parsing failed
        {
            sendError(buf[2] /*msgId*/, ec, "decode");
            continue;
        }

        uint8_t id = msg.hdr.id;
        MediaContainer* out = nullptr;

        switch (msg.payload.tag)
        {
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

            case DProtocol::TAG_OPTION_LIST:
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
        sendAck(id);                               // generic ACK for every good packet
    }
    return ready;
}

// ─────────────────────────── TextBatch handler ────────────────────────
inline MediaContainer* SPIDriver::handle(const DProtocol::TextBatch& tb)
{
    auto* group = new TextGroup(0, DICE_DARKGREY, DICE_WHITE);

    for (uint8_t i = 0; i < tb.itemCount; ++i)
    {
        const DProtocol::TextItem& it = tb.items[i];
        Text* t = new Text(
            String(reinterpret_cast<const char*>(it.text), it.len),
            0,
            static_cast<FontID>(it.font),
            it.x,
            it.y
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

} // namespace dice
#endif
