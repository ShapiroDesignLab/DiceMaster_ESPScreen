# DiceMaster_ESPScreen

## Changes Since Aug 10, 2025 (NOT TESTED)

The following commits have been made after Aug 10, 2025 and have not yet been tested:

- 352d5e9 Fix malformed .gitignore entry for .worktrees/
- 3fa1791 Add project documentation framework (ADRs, architecture, runbooks)
- 0c21963 feat: merge H.264 video support into main
- 82b9bb2 docs: add H.264 video support planning document
- 8168b08 docs: update library versions to reflect Core 3.x upgrade
- d0ccdf2 Fix all compilation errors for Arduino Core 3.x
- 67b5bdd Strip esp_h264 to decoder-only for Arduino build
- 4eddc6b Restore u8g2_font_unifont_t_chinese (revert rename)
- 1fce2bd Fix compilation errors for Arduino Core 3.x
- efd4dfb fix: remove PROGMEM from gen_test_video.py output
- 88f109b fix: remove enqueue double-free; copy video to PSRAM before decode; remove PROGMEM
- f8b7925 fix: delete VideoFrame on enqueue failure to follow project ownership contract
- e0f7724 feat: add test_video_playback() — smoke test for H.264 decode + 2x upscale pipeline
- 4b90cf3 feat: add gen_test_video.py script for generating H.264 test video header
- a852611 feat: add test_video.h — embedded H.264 Annex-B for rotating U-M logo (240×240, 24fps)
- cf51a3c feat: add Screen::draw_bmp565_2x for 2x upscaled display
- 2e50341 feat: add upscale_bmp565_2x and yuv420_to_rgb565_2x helpers; optimize JPEGDraw240
- 791ee3b feat: wire EspH264Decoder into VideoStream, remove old h264bsd files
- 68b1861 fix: EspH264Decoder safety — delete copy ops, check consume bounds, reorder error check, check out_size
- 8bc98f1 feat: add EspH264Decoder wrapping espressif/esp_h264
- ee38b7a fix: remove dead screen_buffer, check canvas begin(), clarify pin comments
- e9c26c1 feat: migrate display to esp_lcd + Arduino_Canvas
- f345dfe feat: add tinyh264 prebuilt library, clean up esp_h264 component
- 9cfce3e feat: add espressif/esp_h264 component source
- 329f21c Add implementation plan: Core 3.x + H.264 + 2x upscaling
- 977ee3f Add embedded test video section to design spec
- d2eec99 Add design spec: Core 3.x upgrade + H.264 + 2x upscaling
- c8f5298 Fix final review issues: frame dispatch, current_disp flush, stub guards
- 5cb6441 Task 6: Add video message handlers to DecodingHandler
- a435821 Task 6: Add video message handlers to DecodingHandler
- ae99779 Task 5: Fix pool race, frame buf cap, and alloc logging in VideoStream
- 467ad5f Task 5: Add VideoStream class and esp_h264 stub
- 8fb8899 Task 4: Fix protocol dispatch and endianness for video message types
- 90a820d Task 4: Add video protocol payload structs to protocol.h
- ee251d9 Task 3: Add flush_video_stream and VIDEO rendering to Screen
- 52139af Task 3: Add flush_video_stream and VIDEO case to Screen
- a50f078 Task 2: Fix YUV luma adjustment and destructor null safety
- 2302961 Task 2: Add VideoFrame class to media.h/media.cpp
- 0d2f6bd Add video MessageType and MediaType enum values to constants.h
- 76807f1 Add .worktrees/ to gitignore
- 99b419d Add setup.md (extracted from root software.md)
- f405a7c Add CLAUDE.md and .claude/ to gitignore
- 0abed78 Updated to include default orientations

This repository contains code for the screens powered by ESP32 boards for the U-M Shapiro Design Lab Programmable Dice Project (2024). 
