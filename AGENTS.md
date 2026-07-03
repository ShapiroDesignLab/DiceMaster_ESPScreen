# Guidance for Coding Agents

**Do not keep separate documentation in this file.** All project documentation
lives in the README and `docs/`, which are the single source of truth for both
humans and agents. If something is missing or wrong, fix it there — not here.

## Where to look

| You need… | Read |
|---|---|
| Overview, hardware, libraries, repo layout | [`README.md`](README.md) |
| How the firmware works (pipeline, protocol, media types, modes) | [`docs/architecture.md`](docs/architecture.md) |
| Toolchain and exact library versions | [`docs/setup.md`](docs/setup.md) |
| Building & flashing a board, factory reset | [`docs/runbooks/flashing.md`](docs/runbooks/flashing.md) |
| Compile/flash/debug loop, serial logs, SPI_DEBUG, end-to-end test | [`docs/runbooks/local-dev.md`](docs/runbooks/local-dev.md) |
| The SPI wire protocol (shared with the Pi) | root repo `docs/protocol.md` |

## Working notes

- This is Arduino/ESP32-S3 firmware (Adafruit Qualia). The board is an **SPI
  slave** receiving media commands from `DiceMaster_Central` (Raspberry Pi).
- The wire protocol is **shared with Central** — changes to `protocol.h` /
  `constants.h` must stay in sync with Central's `media_typing/protocol.py` and
  the root repo's `docs/protocol.md`.
- Exact library versions matter; see `docs/setup.md`.
- When you change behavior, update the relevant file under `docs/` (and the
  README if the overview changed) as part of the same change.
