# ADR-003: U8g2 for Multi-Language Text Rendering

## Status

Accepted

## Context

DiceMaster displays game content on dice faces. The system is designed for an international audience and the Pi master can send text in any of the following scripts via the `TEXT_BATCH` SPI message:

- Latin/Unicode (general fallback)
- Arabic
- Chinese (Traditional/Simplified subset)
- Cyrillic
- Devanagari

The `TEXT_BATCH` protocol field carries a `font` byte that maps to a `FontID` enum:

```cpp
enum class FontID : uint8_t {
    NOTEXT     = 0,
    TF         = 1,   // Latin/Unicode
    ARABIC     = 2,
    CHINESE    = 3,
    CYRILLIC   = 4,
    DEVANAGARI = 5
};
```

The firmware must select and render the correct glyph set at runtime based on the incoming font ID. The 480x480 display draws text via the `Arduino_Canvas` framebuffer, so the rendering library must be able to write directly to a pixel buffer.

The ESP32-S3 on the Adafruit Qualia board has 8 MB of PSRAM. The PSRAM holds the decoded RGB565 framebuffer (480 × 480 × 2 = ~461 KB), leaving room for font data only if it is compact.

## Decision

Use **U8g2 2.35.30** with its bundled bitmap font collection. `Text::map_font()` in `media.h` selects the appropriate font pointer at render time:

```cpp
static const uint8_t* map_font(FontID font_id) {
    switch (font_id) {
    case FontID::TF:         return u8g2_font_unifont_tf;
    case FontID::ARABIC:     return u8g2_font_unifont_t_arabic;
    case FontID::CHINESE:    return u8g2_font_unifont_t_chinese3;
    case FontID::CYRILLIC:   return u8g2_font_cu12_t_cyrillic;
    case FontID::DEVANAGARI: return u8g2_font_unifont_t_devanagari;
    default:                 return u8g2_font_unifont_tf;
    }
}
```

U8g2 is used in "buffer mode" backed by the existing `Arduino_Canvas` framebuffer. The library writes glyphs into the canvas, which is then pushed to the physical display panel. Text rotation is handled by `Screen::map_text_rotation()`, which swaps 90° and 270° to compensate for the physical display orientation.

The U8g2 library is installed through the Arduino Library Manager (version 2.35.30), which makes it part of the standard build without any custom toolchain steps.

## Consequences

**Benefits:**

- The five required scripts (Latin, Arabic, Chinese, Cyrillic, Devanagari) are all available as pre-compiled bitmap font arrays in the U8g2 distribution; no font compilation pipeline or external file system is needed.
- Font data lives in flash (program memory), not RAM or PSRAM, so it does not compete with the framebuffer allocation.
- U8g2's API is straightforward to use with an existing pixel buffer, which fits the `Arduino_Canvas` + `esp_lcd_panel_rgb` display stack.
- The library is actively maintained and version-pinned in the dependency list, making builds reproducible.

**Trade-offs:**

- U8g2 provides bitmap fonts only. Glyph shapes are fixed at the compiled pixel size; scaling produces blocky results. This is acceptable for dice face display, where text is rendered at a predictable size.
- The Chinese font (`u8g2_font_unifont_t_chinese3`) covers only a subset of CJK characters (the Unifont subset). Characters outside this set will render as missing-glyph boxes. Extending coverage would require a larger font or a second font layer.
- Arabic and Devanagari bitmap rendering does not apply ligature shaping or bidirectional layout. For short labels on dice faces this is acceptable, but complex sentences would render incorrectly.

## Alternatives Considered

**TrueType/OpenType fonts via LVGL**
LVGL supports vector font rendering with proper shaping (including Arabic ligatures and bidirectional text). Rejected because LVGL requires significant RAM for its widget system and internal buffers. The ESP32-S3 does not have enough free internal RAM to run LVGL alongside the existing FreeRTOS task set and DMA buffer pool.

**Adafruit GFX font system**
Adafruit GFX provides a compact bitmap font format but ships only Latin character sets. Adding Arabic, Chinese, Cyrillic, and Devanagari would require sourcing or converting third-party font files and integrating a custom shaping pass. U8g2 provides all of these out of the box.

**FreeType on-device rendering**
FreeType can render any TrueType font and supports proper script shaping via HarfBuzz. Both libraries together exceed the available flash and RAM headroom of the ESP32-S3 when combined with the existing firmware, and build integration with arduino-esp32 is non-trivial.
