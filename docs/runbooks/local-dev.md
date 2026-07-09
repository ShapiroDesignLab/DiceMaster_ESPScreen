# Runbook: Local Development & Debugging

The compile → flash → observe loop for the ESPScreen firmware, how to read the
serial logs, and how to test end-to-end with the Raspberry Pi.

## The loop

1. Edit code in `Dicemaster/`.
2. Build + upload from the Arduino IDE (see `docs/runbooks/flashing.md`).
3. Watch the **Serial Monitor at 115200 baud**.
4. Repeat. If an upload fails after a crash, factory-reset the board
   (`docs/runbooks/flashing.md` → "Factory reset").

For fast iteration without the Pi, flash with `current_mode = DEMO` (built-in
animations) or `TESTING` (runs `tests.h`).

## Reading the serial log

The firmware logs with bracketed subsystem tags. The ones you'll see most:

| Tag | Meaning |
|---|---|
| `[SPI]` | `SPIDriver` setup — DMA buffer allocation, pipeline init |
| `[SPI-DECODE]` | Decode task — per-transaction processing, transaction errors |
| `[SPI-REQUEUE]` | Buffer being returned to the SPI hardware pool |

A healthy PRODUCTION boot looks roughly like:

```
[SPI] Allocated buffer 0 with ID 0
... (16 buffers)
[SPI] Initialized with 16 DMA buffers for event-driven pipeline
[SPI-DECODE] Event-driven decode task started
[SPI] Queued initial buffer ID 0
...
[SPI] Event-driven pipeline initialized successfully in constructor
```

If you see `FATAL` lines (`Failed to allocate DMA buffer`, `Failed to initialize
decoding handler`, `Failed to create decode task`) the firmware halts in a
`while(1)` loop by design — the cause is almost always PSRAM not enabled or the
wrong partition scheme. Recheck the board settings in `docs/runbooks/flashing.md`.

## SPI_DEBUG mode

Flash with `current_mode = SPI_DEBUG` to print transport statistics every ~3
seconds: transaction count, buffers processed, and decode errors (from
`SPIDriver::get_driver_statistics()` / `get_decode_statistics()`). Use it to
answer:

- **Is the Pi actually clocking data in?** — transaction count should climb.
- **Are messages being dropped?** — a rising decode-error count points at a
  protocol mismatch, framing error, or buffer overrun.

## End-to-end test with the Pi

1. Flash the board(s) in `PRODUCTION` mode with the correct `SCREEN_ID` per
   board.
2. On the Pi, bring up `DiceMaster_Central` (see that repo's
   `docs/runbooks/deploy.md`).
3. Send a display command from the Pi:

   ```bash
   ros2 topic pub /screen_1_cmd dicemaster_central_msgs/msg/ScreenMediaCmd \
     "{screen_id: 1, media_type: 0, file_path: '/absolute/path/to/greeting.json'}" --once
   ```

4. The corresponding board should render the content. If it doesn't, watch the
   board's serial log (`SPI_DEBUG` mode) while sending to see whether bytes are
   arriving.

## Troubleshooting

| Symptom | Likely cause / check |
|---|---|
| Board halts at boot with `FATAL` | PSRAM not `OPI PSRAM`, or partition not "Huge APP". |
| Transactions climb but nothing renders | `SCREEN_ID` bit not set by the sender, or media failed to decode — check `[SPI-DECODE]` errors. |
| Decode-error count rising | Protocol/framing mismatch with Central — confirm both sides match root `docs/protocol.md`. |
| Nothing on serial at all | Wrong baud (use 115200) or wrong port; try a different USB cable. |
| Upload fails right after a crash | Board is stuck — factory-reset it (`docs/runbooks/flashing.md`). |
| Colors/rotation wrong | GFX library version mismatch, or wrong `DEFAULT_ROTATION`. |

## Where things live

See `docs/architecture.md` for the full pipeline and `../README.md` for the
repository layout. Key files: `Dicemaster/spi.h` (transport),
`Dicemaster/decoding_handler.h` (protocol parse), `Dicemaster/screen.h`/`.cpp`
(render), `Dicemaster/protocol.h` + `constants.h` (wire format and enums).
