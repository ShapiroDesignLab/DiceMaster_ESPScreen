#include "Media.h"

namespace dice {

void MediaContainer::set_status(MediaStatus new_status) {
    std::lock_guard<std::mutex> lock(status_mtx);
    status = new_status;
}

MediaContainer::MediaContainer(MediaType med_type, size_t dur)
    : media_type(med_type)
    , status(MediaStatus::NOT_RECEIVED)
    , duration(dur)
    , start_time(0) 
    {}

MediaContainer::~MediaContainer() {}

MediaType MediaContainer::get_media_type() const {
    return media_type;
}

MediaStatus MediaContainer::get_status() {
    bool expired = (start_time > 0 && (millis() - start_time) >= duration);
    if (expired) {
        set_status(MediaStatus::EXPIRED);
    }
    std::lock_guard<std::mutex> lock(status_mtx);
    return status;
}

void MediaContainer::trigger_display() {
    if (start_time != 0) return;   // Only trigger once
    set_status(MediaStatus::DISPLAYING);
    start_time = max(millis(), (long unsigned int) 1);
    get_status();
}


Text::Text(String input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy)
    : MediaContainer(MediaType::TEXT, duration)
    , content(input)
    , font_id(ft_id)
    , cursor_x(cx)
    , cursor_y(cy) {
    set_status(MediaStatus::READY);
}

Text::Text(char* input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy)
    : MediaContainer(MediaType::TEXT, duration)
    , content(String(input))
    , font_id(ft_id)
    , cursor_x(cx)
    , cursor_y(cy) {
    set_status(MediaStatus::READY);
}

// APIs for Text
const uint8_t* Text::get_font() const {
    return map_font(font_id);
}
uint16_t Text::get_cursor_x() const {
    return cursor_x;
}
uint16_t Text::get_cursor_y() const {
    return cursor_y;
}
String Text::get_txt() const {
    return content;
}
FontID Text::get_font_id() const {
    return font_id;
}


TextGroup::TextGroup(size_t dur, uint16_t bg_col, uint16_t font_col)
    : MediaContainer(MediaType::TEXTGROUP, dur)
    , next_idx(0)
    , bg_color(bg_col)
    , font_color(font_col) {
    set_status(MediaStatus::READY);
}

TextGroup::~TextGroup() {
    for (auto txt : vec) {
      delete txt;
    }
}

void TextGroup::add_member(MediaContainer* txt) {
    vec.push_back(txt);
}

size_t TextGroup::size() const {
    return vec.size();
}

MediaContainer* TextGroup::get_next() {
    if (vec.empty() || next_idx >= vec.size()) {
        return nullptr;
    }
    return vec[next_idx++];
}

uint16_t TextGroup::get_bg_color() const {
    return bg_color;
}

uint16_t TextGroup::get_font_color() const {
    return font_color;
}


size_t Image::received_len() {
    return input_ptr - content;
}

int Image::JPEGDraw480(JPEGDRAW* pDraw) {
    // Serial.println("Drawing");
    Image* img = static_cast<Image*>(pDraw->pUser);
    img->decode_mtx.lock();
    // uint16_t* destination = img->decoded_content + (pDraw->y * 480 + pDraw->x);
    // memcpy(destination, pDraw->pPixels, pDraw->iWidth * pDraw->iHeight * sizeof(uint16_t));
    // Serial.println("Plotting chunk");
    for (int row = 0; row < pDraw->iHeight; row++) {
        // Calculate the start of the line in the final buffer
        uint16_t* dest_line = img->decoded_content + (pDraw->y + row) * 480 + pDraw->x;
        // Calculate the source line within pPixels
        const uint16_t* src_line = ((uint16_t*)pDraw->pPixels) + row * pDraw->iWidth;

        memcpy(dest_line, src_line, pDraw->iWidth * sizeof(uint16_t));
    }
    img->decode_mtx.unlock();
    return 1;   // continue decode
}

int Image::JPEGDraw240(JPEGDRAW* pDraw) {
    Image* img = static_cast<Image*>(pDraw->pUser);
    img->decode_mtx.lock();

    for (int row = 0; row < pDraw->iHeight; row++) {
        // 1) Figure out the source pointer for this row in the chunk
        const uint16_t* src_line = 
            reinterpret_cast<const uint16_t*>(pDraw->pPixels) + row * pDraw->iWidth;

        // 2) Compute the *upscaled* row in the final 480×480 buffer
        //    Each Y in [0..239] maps to 2*Y in [0..478].
        int scaled_y = 2 * (pDraw->y + row);

        // 3) Destination pointer for the first (upper) row of the 2×2 block
        uint16_t* dest_line = img->decoded_content 
                              + (scaled_y * 480) 
                              + (2 * pDraw->x); 

        // 4) Expand horizontally by copying each source pixel twice
        //    (Because each X in [0..239] maps to 2*X in [0..478].)
        for (int col = 0; col < pDraw->iWidth; col++) {
            uint16_t px = src_line[col];
            dest_line[2*col]     = px;  // repeat horizontally
            dest_line[2*col + 1] = px;
        }
        // Copy things over
        memcpy(dest_line + 480, 
               dest_line, 
               (2 * pDraw->iWidth) * sizeof(uint16_t));
    }

    img->decode_mtx.unlock();
    return 1;   // continue decode
}


void Image::decode() {
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);   // Adjust as necessary
    // Set the drawing function
    int (*JPEGDraw) (JPEGDRAW*);
    if (resolution == ImageResolution::SQ240) {
      Serial.println("Decoding 240x240");
      JPEGDraw = JPEGDraw240; 
    }
    else{
      JPEGDraw = JPEGDraw480; 
      Serial.println("Decoding 480x480");
    }
    // Decode
    if (jpeg.openRAM(content, total_size, JPEGDraw)) {
        // Serial.println("Decoding");
        jpeg.setUserPointer(this);
        if (jpeg.decode(0, 0, 0)) {}   // Decode at full scale
        jpeg.close();
        // free(content);   // Free original content as it's no longer needed
        // content = nullptr;
        set_status(MediaStatus::READY);
    } else {
        Serial.println("[Warning] Bad image file");
        set_status(MediaStatus::EXPIRED);   // Handle error appropriately
    }
}

void Image::startDecode() {
    set_status(MediaStatus::DECODING);
    xTaskCreatePinnedToCore(decodeTask, "DecodeTask", 8192, this, 1, &decodeTaskHandle, 0);
    // Serial.println("Task started");
}

Image::Image(uint8_t img_id, ImageFormat format, ImageResolution res, uint32_t total_img_size, size_t duration)
    : MediaContainer(MediaType::IMAGE, duration)
    , image_id(img_id)
    , image_format(format)
    , resolution(res)
    , total_size(total_img_size)
    , content((uint8_t*) ps_malloc(total_img_size))
    , input_ptr(content)
    , decoded_content((uint16_t*) ps_malloc(SCREEN_PXLCNT * sizeof(uint16_t)))
    , decodeTaskHandle(nullptr) {
    if (content == nullptr || decoded_content == nullptr) {
        // Handle memory allocation failure
        set_status(MediaStatus::EXPIRED);
    }
}

Image::~Image() {
    free(content);
    content = nullptr;
    free(decoded_content);
    decoded_content = nullptr;
}

uint16_t* Image::get_img() {
    if (get_status() < MediaStatus::READY) {
        return nullptr;
    }
    return decoded_content;
}

void Image::add_chunk(const uint8_t* chunk, size_t chunk_size) {
    if (input_ptr + chunk_size > content + total_size) {
        return;
    }
    memcpy(input_ptr, chunk, chunk_size);
    input_ptr += chunk_size;

    if (received_len() == total_size) {
        if (image_format == ImageFormat::JPEG) {
            // Serial.println("Started decoding");
            startDecode();
        } else if (image_format == ImageFormat::RGB565) {
            // For RGB565 bitmap, no decoding needed
            memcpy(decoded_content, content, total_size);
        }
        // free(content);
        // content = nullptr;
    }
}

uint8_t Image::get_image_id() const {
    return image_id;
}

ImageFormat Image::get_image_format() const {
    return image_format;
}

ImageResolution Image::get_image_resolution() const {
    return resolution;
}

void Image::add_decoded(const uint16_t* img) {
  memcpy(decoded_content, img, SCREEN_PXLCNT * sizeof(uint16_t));
  set_status(MediaStatus::READY);
}

// OptionGroup::OptionGroup(uint8_t selected_idx)
//     : MediaContainer(MediaType::OPTION, 0)
//     , selected_index(selected_idx) {
//     set_status(MediaStatus::READY);
// }

// void OptionGroup::add_option(String option_text) {
//     options.push_back(option_text);
// }

// size_t OptionGroup::size() const {
//     return options.size();
// }

// std::vector<String> OptionGroup::get_option_text(uint8_t select_id) const {
//     if (options.size() <= 1) {
//         return options;
//     }
//     if (select_id == 0) {
//         return std::vector<String>(String(), options[0], options[1]);
//     }
//     if (select_id == options.size()-1) {
//         return std::vector<String>(options[select_id-1], options[select_id], String());
//     }
//     return std::vector<String>(options[select_id-1], options[select_id], options[select_id+1]);
// }

// uint8_t OptionGroup::get_selected_index() const {
//     return selected_index;
// }

// void OptionGroup::set_selected_index(uint8_t idx) {
//     if (idx < options.size()) {
//         selected_index = idx;
//     }
// }

// void OptionUpdate::OptionUpdate(const uint8_t new_id) 
//     : selecting_id(new_id)
//     {}

// virtual uint8_t OptionUpdate::get_selected_index() const {
//     return selecting_id;
// }

// Demo Functions
MediaContainer* get_demo_textgroup() {
    TextGroup* group = new TextGroup(0, DARKGREY, WHITE);
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

MediaContainer* print_error(String input) {
    TextGroup* group = new TextGroup(0, DARKGREY, WHITE);
    group->add_member(new Text("DEBUG Info:", 0, FontID::TF, 40, 40));
    group->add_member(new Text(input, 0, FontID::TF, 40, 160));
    Serial.println("[ERROR]: " + input);
    return group;
}

}   // namespace dice
