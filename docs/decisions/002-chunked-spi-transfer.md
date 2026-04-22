# ADR-002: Chunked SPI Image Transfer

## Status

Accepted

## Context

The DiceMaster Pi master sends JPEG images to each ESP32 screen board over SPI. A 480x480 JPEG at typical quality settings is 20–50 KB. The SPI DMA buffers on the ESP32-S3 are allocated at firmware startup in the `SPIDriver` constructor:

```cpp
constexpr size_t SPI_BUFFER_SIZE = 8192;   // 8 KB per DMA buffer
constexpr size_t BUFFER_POOL_SIZE = 16;    // 16 buffers in the pool
```

Each SPI transaction can transfer at most `SPI_BUFFER_SIZE` (8 KB) of payload. A single DMA transaction therefore cannot carry a full JPEG file.

The protocol also needs to be self-framing: the ESP32 must know the total expected size and the number of pieces in advance so it can allocate the receive buffer and detect a complete transfer without relying on a timeout alone.

## Decision

Split image data into fixed-size chunks on the Pi side and send them as a sequence of SPI messages:

1. **`IMAGE_TRANSFER_START` (0x02)** — carries the image ID, format (`JPEG` or `RGB565`), resolution (`SQ480` or `SQ240`), total byte size, total chunk count, display duration, rotation, and **embeds chunk 0** in its payload. This means single-chunk images require only one message.

2. **`IMAGE_CHUNK` (0x03)** × (numChunks − 1) — each carries the image ID, a sequential chunk ID (starting from 1, since chunk 0 is embedded in START), a 24-bit byte offset, and the chunk data.

3. **`IMAGE_TRANSFER_END` (0x04)** — optional; the firmware also detects completion by comparing `received_chunks[imgId]` against `expected_chunks[imgId]`.

On the ESP32, `DecodingHandler` maintains an `ongoing_transfers` map keyed by `imgId`. The `Image` object pre-allocates a contiguous PSRAM buffer of `totalSize` bytes and fills it as chunks arrive via `add_chunk_with_id()`. A per-chunk received mask tracks which chunks have been written. When `received_chunks >= expected_chunks`, the complete `Image` is handed to the screen queue, which triggers asynchronous JPEG decode.

The Image constructor also sets a per-transfer timeout:

```cpp
chunk_timeout_ms = 100 * num_chunks;
```

Transfers that stall (e.g. due to a lost SPI transaction) are invalidated by `check_transfer_timeout()` so the buffer and image ID can be recycled.

## Consequences

**Benefits:**

- Arbitrary image sizes are supported regardless of the 8 KB DMA buffer limit.
- The START message embeds chunk 0, so single-chunk images (small JPEGs or thumbnails) incur no extra round-trip.
- The receiver knows `numChunks` upfront and can pre-allocate exactly `totalSize` bytes in PSRAM, avoiding fragmentation from incremental `realloc` calls.
- Chunk sequence gaps are detectable (`expected_chunk_id = received_count`), making debugging easier.
- The protocol is compatible with broadcast delivery: a single Pi SPI transaction can carry a chunk that all boards on the bus receive simultaneously, addressed via the `screenId` bitmask in the header.

**Trade-offs:**

- Adds protocol state machine complexity: `DecodingHandler` must maintain per-image chunk counts, received masks, start times, and ongoing transfer maps across multiple SPI transactions.
- The `IMAGE_TRANSFER_END` message is optional, which means the completion condition is purely count-based; a corrupted chunk that passes header validation but carries wrong data will silently produce a corrupt image.
- Chunk sequence numbers start at 1 for separate `IMAGE_CHUNK` messages (0 is always embedded in START), which is a non-obvious convention that requires care on the Pi encoder side.

## Alternatives Considered

**Increase SPI buffer size beyond 8 KB**
The ESP32-S3 DMA engine imposes hardware limits on single-transaction size. Allocating 64 KB DMA-capable buffers would exhaust the internal DMA-capable SRAM and is not practical with the 16-buffer pool design.

**Compress images further to fit in one transaction**
A 480x480 JPEG at very low quality can sometimes fit in 8 KB, but this degrades visual quality unacceptably for dice face images. Thumbnail-resolution images (240x240) at moderate quality still exceed 8 KB in many cases.

**Use a higher-level transport (USB CDC, Wi-Fi)**
Rejected to keep latency low and wiring simple. The board is already connected to the Pi via SPI for the primary control bus; adding a second interface would increase hardware complexity and latency.
