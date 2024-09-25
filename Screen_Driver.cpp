#include "Screen_Driver.h"
#include "imageh/rgb565_umlogo.h"

namespace dice {
Screen::Screen()
  : expander(new Arduino_XCA9554SWSPI(PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI,&Wire, 0x3F))
  , rgbpanel(new Arduino_ESP32RGBPanel(
      TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
      TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
      TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
      TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,
      1 /* hsync_polarity */, 50 /* hsync_front_porch */, 2 /* hsync_pulse_width */, 44 /* hsync_back_porch */,
      1 /* vsync_polarity */, 16 /* vsync_front_porch */, 2 /* vsync_pulse_width */, 18 /* vsync_back_porch */
      ))
  , gfx(new Arduino_RGB_Display(
  // 4.0" 480x480 rectangle bar display
    480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED /* RST */, tl040wvs03_init_operations, sizeof(tl040wvs03_init_operations)))
  , current_disp(nullptr)
  , backlight_status(BACKLIGHT_ON)
{
    if (!gfx->begin()) {
        Serial.println("gfx->begin() failed!");
    }

    Wire.setClock(1000000); // speed up I2C

    gfx->fillScreen(BLACK);
    gfx->setUTF8Print(true);

    expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
}


Screen::~Screen() {
    delete gfx;
    delete rgbpanel;
    delete expander;
}

bool Screen::is_next_ready() {
    while (!display_queue.empty()) {
        MediaContainer* media = display_queue.front();
        if (media->get_status() == MediaStatus::READY) {
            return true;
        } else if (media->get_status() == MediaStatus::EXPIRED) {
            // Remove expired media
            display_queue.pop_front();
            delete media;
        } else {
            break;
        }
    }
    return false;
}

void Screen::display_next() {
    if (current_disp != nullptr) {
        delete current_disp;
        current_disp = nullptr;
    }

    if (!display_queue.empty()) {
        current_disp = display_queue.front();
        display_queue.pop_front();

        switch (current_disp->get_media_type()) {
            case MediaType::IMAGE:
                draw_image(current_disp);
                break;
            case MediaType::TEXTGROUP:
                draw_textgroup(current_disp);
                break;
            case MediaType::OPTION:
                draw_optiongroup(current_disp);
                break;
            case MediaType::BACKLIGHT_CONTROL:
                handle_backlight_control(current_disp);
                break;
            default:
                Serial.println("Unsupported Media Type Encountered!");
                break;
        }

        current_disp->trigger_display();
    }
}

void Screen::draw_image(MediaContainer* media) {
    if (media->get_media_type() != MediaType::IMAGE) return;
    uint16_t* img_data = media->get_img();
    if (img_data) {
        gfx->draw16bitRGBBitmap(0, 0, img_data, gfx->width(), gfx->height());
    }
}

void Screen::draw_textgroup(MediaContainer* media) {
    if (media->get_media_type() != MediaType::TEXTGROUP) return;
    TextGroup* text_group = static_cast<TextGroup*>(media);

    MediaContainer* txt_media = nullptr;
    while ((txt_media = text_group->get_next()) != nullptr) {
        Text* txt = static_cast<Text*>(txt_media);
    }
}

void Screen::draw_optiongroup(MediaContainer* media) {
    if (media->get_media_type() != MediaType::OPTION) return;
    OptionGroup* option_group = static_cast<OptionGroup*>(media);

    uint8_t selected_index = option_group->get_selected_index();
    size_t num_options = option_group->size();

    // Calculate indices for options to display
    int option_indices[3];
    option_indices[1] = selected_index; // Middle option (selected)
    option_indices[0] = (selected_index > 0) ? selected_index - 1 : -1; // Option above
    option_indices[2] = (selected_index + 1 < num_options) ? selected_index + 1 : -1; // Option below

    // Positions for the three labels
    int label_positions[3] = {
        gfx->height() / 2 - 60, // Above
        gfx->height() / 2 - 20, // Middle
        gfx->height() / 2 + 20  // Below
    };

    // Create or update labels
    for (int i = 0; i < 3; ++i) {

    }

    // Scroll animation if the selected index has changed
    static uint8_t last_selected_index = 0xFF;
    if (last_selected_index != 0xFF && last_selected_index != selected_index) {
        // Determine scroll direction
        int delta = selected_index - last_selected_index;
    }

    last_selected_index = selected_index;
}

void Screen::handle_backlight_control(MediaContainer* media) {
    // Assuming MediaContainer has necessary info for backlight control
    set_backlight(backlight_status);
}

void Screen::enqueue(MediaContainer* media) {
    if (!media) return;

    // Handle special media types immediately
    if (media->get_media_type() == MediaType::BACKLIGHT_CONTROL) {
        handle_backlight_control(media);
        delete media;
        return;
    }

    // Enqueue other media types
    display_queue.push_back(media);
}

void Screen::update() {
    if (is_next_ready()) {
        if (!current_disp || current_disp->get_status() == MediaStatus::EXPIRED) {
            display_next();
        } else if (current_disp->get_media_type() == MediaType::OPTION) {
            // Option selection might have been updated
            draw_optiongroup(current_disp);
        }
    }
}

void Screen::set_backlight(bool status) {
    backlight_status = status;
    if (status == BACKLIGHT_ON) {
        expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
    } else {
        expander->digitalWrite(PCA_TFT_BACKLIGHT, LOW);
    }
}

bool Screen::down_button_pressed() {
    return !expander->digitalRead(PCA_BUTTON_DOWN);
}

bool Screen::up_button_pressed() {
    return !expander->digitalRead(PCA_BUTTON_UP);
}

void Screen::draw_startup_logo() {
    MediaContainer* med = new Image((size_t)1, ImageFormat::BMP480, (size_t)0, (size_t)0);
    med->add_decoded(umlogo);
    enqueue(med);
  }


// Demo Functions
MediaContainer* get_demo_textgroup() {
  TextGroup* group = new TextGroup(0, RED);
  group->add_member(new Text("Psíquico", 0, FontID::TF, 40, 40));
  group->add_member(new Text("Hellseher", 0, FontID::TF, 280, 40));
  group->add_member(new Text("экстрасенс", 0, FontID::CYRILLIC, 40, 160));
  group->add_member(new Text("Psychique", 0, FontID::TF, 280, 160));
  group->add_member(new Text("Psychic", 0, FontID::TF, 40, 280));
  group->add_member(new Text("मानसिक", 0, FontID::DEVANAGARI, 280, 280));
  group->add_member(new Text("靈媒", 0, FontID::CHINESE, 40, 400));
  group->add_member(new Text("نفسية", 0, FontID::ARABIC, 280, 400));
  return group;
}

}