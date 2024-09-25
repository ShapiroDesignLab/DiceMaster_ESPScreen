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
  {
      // Limit duration if necessary
  }

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
    start_time = max(millis(), (long unsigned int)1);
    get_status();
}


Text::Text(String input, size_t duration, FontID ft_id, uint16_t cx, uint16_t cy)
      : MediaContainer(MediaType::TEXT, duration)
      , content(input)
      , font_id(ft_id)
      , cursor_x(cx)
      , cursor_y(cy)
  {
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
        , font_color(font_col)
{
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

int Image::JPEGDraw(JPEGDRAW *pDraw) {
    Image *img = static_cast<Image*>(pDraw->pUser);
    img->decode_mtx.lock();
    uint16_t* destination = img->decoded_content + (pDraw->y * 480 + pDraw->x);
    memcpy(destination, pDraw->pPixels, pDraw->iWidth * pDraw->iHeight * sizeof(uint16_t));
    img->decode_mtx.unlock();
    return 1; // continue decode
}

static void Image::decodeTask(void* pvParameters) {
    Image* img = static_cast<Image*>(pvParameters);
    img->decode();
    vTaskDelete(nullptr); // Delete task after completion
}

void Image::decode() {
    jpeg.setPixelType(RGB565_BIG_ENDIAN); // Adjust as necessary
    if (jpeg.openRAM(content, content_len, JPEGDraw)) {
        jpeg.setUserPointer(this);
        if (jpeg.decode(0, 0, 0)) { // Decode at full scale
            set_status(MediaStatus::READY);
        }
        jpeg.close();
        free(content); // Free original content as it's no longer needed
        content = nullptr;
    } else {
        set_status(MediaStatus::EXPIRED); // Handle error appropriately
    }
}

void Image::startDecode() {
    set_status(MediaStatus::DECODING);
    xTaskCreatePinnedToCore(decodeTask, "DecodeTask", 8192, this, 1, &decodeTaskHandle, 0);
}


Image::Image(uint8_t img_id, ImageFormat format, uint32_t total_img_size, size_t duration)
    : MediaContainer(MediaType::IMAGE, duration)
    , image_id(img_id)
    , image_format(format)
    , total_size(total_img_size)
    , content((uint8_t*)ps_malloc(total_img_size))
    , content_len(total_img_size)
    , input_ptr(content)
    , decoded_content((uint16_t*)ps_malloc(SCREEN_BUF_SIZE * sizeof(uint16_t)))
    , decodeTaskHandle(nullptr)
{
  if (content == nullptr || decoded_content == nullptr) {
      // Handle memory allocation failure
      set_status(MediaStatus::EXPIRED);
  }
}

Image::~Image() {
    free(content);
    free(decoded_content);
}

uint16_t* Image::get_img() {
    if (get_status() < MediaStatus::READY) {
        return nullptr;
    }
    return decoded_content;
}

void Image::add_chunk(uint16_t chunk_number, uint8_t* chunk, size_t chunk_size) {
    if (input_ptr + chunk_size > content + content_len) {
        // Handle overflow
        return;
    }
    memcpy(input_ptr, chunk, chunk_size);
    input_ptr += chunk_size;

    if (received_len() == content_len) {
        if (image_format == ImageFormat::JPEG480 || image_format == ImageFormat::JPEG240) {
            startDecode();
        } else if (image_format == ImageFormat::BMP480 || image_format == ImageFormat::BMP240) {
            // For RGB565 bitmap, no decoding needed
            memcpy(decoded_content, content, content_len);
            free(content);
            content = nullptr;
            set_status(MediaStatus::READY);
        }
    }
}

uint8_t Image::get_image_id() const {
    return image_id;
}

ImageFormat Image::get_image_format() const {
    return image_format;
}


OptionGroup::OptionGroup(uint8_t selected_idx)
    : MediaContainer(MediaType::OPTION, 0)
    , selected_index(selected_idx)
{
    set_status(MediaStatus::READY);
}

void OptionGroup::add_option(String option_text) {
    options.push_back(option_text);
}

size_t OptionGroup::size() const {
    return options.size();
}

String OptionGroup::get_option_text(uint8_t id) const {
    if (id >= options.size()) return String();
    return options[id];
}

uint8_t OptionGroup::get_selected_index() const {
    return selected_index;
}

void OptionGroup::set_selected_index(uint8_t idx) {
    if (idx < options.size()) {
        selected_index = idx;
    }
}

}