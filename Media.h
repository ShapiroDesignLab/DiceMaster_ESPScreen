#ifndef DICE_MEDIA
#define DICE_MEDIA

#include <mutex>
#include <vector>
#include <string>

#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <JPEGDEC.h>


namespace dice {

static const uint8_t STATUS_NOT_RECEIVED = 0;
static const uint8_t STATUS_DECODING = 2;
static const uint8_t STATUS_READY = 3;
static const uint8_t STATUS_DISPLAYING = 4;
static const uint8_t STATUS_EXPIRED = 5;

static const uint8_t MEDIA_TEXT = 0;
static const uint8_t MEDIA_TEXTGROUP = 1;
static const uint8_t MEDIA_IMAGE = 2;
static const uint8_t MEDIA_OPTION = 3;
static const uint8_t MEDIA_OPTION_BLOCKING = 4;
static const uint8_t MEDIA_OPTION_END = 5;
static const uint8_t MEDIA_BACKLIGHT_ON = 253;
static const uint8_t MEDIA_BACKLIGHT_OFF = 254;

static const uint8_t IMG_480 = 0;
static const uint8_t IMG_240 = 1;

static const size_t SCREEN_BUF_SIZE = 480*480;









class MediaContainer {
protected:
  const uint8_t media_type;
  uint8_t status;
  std::mutex status_mtx;
  size_t duration;
  size_t start_time;

  // Private API for setting status
  void set_status(uint8_t new_status) {
    status_mtx.lock();
    status = new_status;
    status_mtx.unlock();
  }


public:
  MediaContainer(const uint8_t med_type, const size_t dur) 
  : media_type(med_type)
  , status(STATUS_NOT_RECEIVED)
  , start_time(0)
  , duration(dur)
  {
    if (duration>255) duration = 0;
  }

  ~MediaContainer() {}

  uint8_t get_media_type() const {
    return media_type;
  }
  // APIs for Options
  virtual uint8_t get_status() {
    bool expired = (start_time > 0 && (millis()-start_time) >= duration);
    if (expired) {
      set_status(STATUS_EXPIRED);
    }
    status_mtx.lock();
    uint8_t tmp_status = status;
    status_mtx.unlock();
    return tmp_status;
  }

  virtual void trigger_display() {
    if (start_time != 0) return;   // Only trigger once
    set_status(STATUS_DISPLAYING);
    start_time = max(millis(), (long unsigned int)1);
    get_status();
  }

  // APIs for Text
  virtual const uint8_t* get_font() const {};
  virtual uint16_t get_cursor_x() const {};
  virtual uint16_t get_cursor_y() const {};
  virtual String get_txt() const {};

  // APIs for Image
  virtual void add_chunk(uint8_t* chunk, size_t chunk_size) {};
  virtual void add_decoded(const uint16_t* img) {};
  virtual uint16_t* get_img() {};

  // APIs for TextGroup
  virtual void add_member(MediaContainer* txt) {};
  virtual MediaContainer* get_next() {};
  virtual uint8_t get_color() const {};
  virtual size_t size() const {};

  // APIs for Animation
  // None at the moment

  
};







class Text : public MediaContainer {
private:
  String content;
  const uint8_t* font;
  const uint16_t cursor_x, cursor_y;
  uint8_t init_mode;

public: 
  Text(String input
        , const size_t duration=0
        , const uint8_t* ft=u8g2_font_unifont_tf
        , const uint16_t cx=40
        , const uint16_t cy=40)
    : MediaContainer(MEDIA_TEXT, duration)
    , content(input)
    , font(ft)
    , cursor_x(cx)
    , cursor_y(cy) 
    {set_status(STATUS_READY);}

  Text(char* input
        , const size_t duration=0
        , const uint8_t* ft=u8g2_font_unifont_tf
        , const uint16_t cx=40
        , const uint16_t cy=40)
    : MediaContainer(MEDIA_TEXT, duration)
    , content(String(input))
    , font(ft)
    , cursor_x(cx)
    , cursor_y(cy)
    {set_status(STATUS_READY);}

  // APIs for Text
  virtual const uint8_t* get_font() const {
    return font;
  }
  virtual uint16_t get_cursor_x() const {
    return cursor_x;
  }
  virtual uint16_t get_cursor_y() const {
    return cursor_y;
  }
  virtual String get_txt() const {
    return content;
  }
};








class Image : public MediaContainer {
private:
  uint8_t* content;
  const size_t content_len;
  uint8_t* input_ptr;

  const uint8_t resolution;

  // Decoding parameters
  uint16_t* decoded_content;
  uint16_t* decode_input_ptr;
  TaskHandle_t decodeTaskHandle;

  JPEGDEC jpeg;
  std::mutex decode_mtx; // Mutex for thread-safe access

  size_t received_len() {return input_ptr - content;}
  
  // JPEGDraw callback function to handle drawing decoded JPEG blocks
  static int JPEGDraw(JPEGDRAW *pDraw) {
    Image *img = static_cast<Image*>(pDraw->pUser);
    img->decode_mtx.lock();
    uint16_t* destination = img->decoded_content + (pDraw->y * 480 + pDraw->x);
    memcpy(destination, pDraw->pPixels, pDraw->iWidth * pDraw->iHeight * sizeof(uint16_t));
    img->decode_mtx.unlock();
    return 1; // continue decode
  }

  static void decodeTask(void* pvParameters) {
    Image* img = static_cast<Image*>(pvParameters);
    img->decode();
    vTaskDelete(nullptr); // Delete task after completion
  }

  void decode() {
    jpeg.setPixelType(RGB565_BIG_ENDIAN); // Set pixel type if JPEG library supports it, adjust as necessary
    if (jpeg.openRAM(content, content_len, JPEGDraw)) {
      // jpeg.setDrawCallback(JPEGDraw); // Set the draw callback
      // jpeg.setUser(this); // Set user pointer to current instance for draw callback
      if (jpeg.decode(0, 0, 0)) { // Decode at full scale
        if (resolution == IMG_240) upscale_2x();   // Upscale if decoded as 240x240 img
        set_status(STATUS_READY);
      }
      jpeg.close();
      delete[] content;     // When decoded, delete original content as it is no longer needed.
      content = nullptr;
    }
  }

  void upscale_2x() {
    for (uint16_t y=239;y>=0; y--) {
      for (uint16_t x=239; x>= 0; x--) {
        const uint16_t pxl = decoded_content[y*240 + x];
        decoded_content[2*y*480 + 2*x] = pxl;
        decoded_content[2*y*480 + 2*x + 1] = pxl;
        decoded_content[(2*y+1)*480 + 2*x] = pxl;
        decoded_content[(2*y+1)*480 + 2*x + 1] = pxl;
      }
    }
  }

  void startDecode() {
    set_status(STATUS_DECODING);
    xTaskCreatePinnedToCore(decodeTask, "DecodeTask", 8192, this, 1, &decodeTaskHandle, 0); // 0 for Core 0, 1 for Core 1
  }

public:
  Image(const size_t raw_len, uint8_t res, const size_t duration)
    : MediaContainer(MEDIA_IMAGE, duration)
    , content((uint8_t*) ps_malloc(raw_len))
    , content_len(raw_len)
    , input_ptr(content)
    , resolution(res)
    , decoded_content((uint16_t*) ps_malloc(480 * 480 * sizeof(uint16_t)))
    , decode_input_ptr(decoded_content)
    , decodeTaskHandle(nullptr)
    {}

  ~Image() {
    free(content);
    free(decoded_content);
  }

  
  virtual uint16_t* get_img() {
    Serial.println("Getting Image");
    if (!get_status() >= STATUS_READY) {
      Serial.println("Content Not Ready Yet!");
      assert(false);
    }
    Serial.println("Content decoded,returning");
    return decoded_content;
  }

  virtual void add_chunk(uint8_t* chunk, size_t chunk_size) {
    if (input_ptr + chunk_size > content + content_len) 
      return;
    memcpy(input_ptr, chunk, chunk_size);
    input_ptr += chunk_size;

    if (received_len() == content_len) startDecode();
  }

  virtual void add_decoded(const uint16_t* img) {
    memcpy(decoded_content, img, SCREEN_BUF_SIZE * sizeof(uint16_t));
    uint16_t* decode_input_ptr;
    set_status(STATUS_READY);
    Serial.print("Status: ");
    Serial.println(status);
  }
};

const uint8_t* map_font(uint8_t key) {
  switch (key) {
    case 2: 
      return u8g2_font_unifont_t_arabic;
    case 3:
      return u8g2_font_unifont_t_chinese;
    case 4:
      return u8g2_font_cu12_t_cyrillic;
    case 5:
      return u8g2_font_unifont_t_devanagari;
    default:
      return u8g2_font_unifont_tf;
  }
}










class TextGroup : public MediaContainer {
private:
  std::vector<MediaContainer*> vec;
  size_t next_idx;
  const uint8_t color;

public:
  TextGroup(const size_t dur, const uint8_t col) 
  : MediaContainer(MEDIA_TEXTGROUP, duration)
  , next_idx(0)
  , color(col)
  {
    set_status(STATUS_READY);
  }

  virtual void add_member(MediaContainer* txt) {
    vec.push_back(txt);
  }

  virtual size_t size() const {
    return vec.size();
  }

  virtual MediaContainer* get_next() {
    if (vec.empty() || vec.size() == next_idx) {
      return nullptr;
    }
    return vec[next_idx++];
  }

  virtual uint8_t get_color() const {
    return color;
  }
};













class OptionGroup : public MediaContainer {
private:
  std::vector<MediaContainer*> vec;
  const uint8_t selecting;

public:
  OptionGroup(const uint8_t sel) 
    : MediaContainer(MEDIA_TEXTGROUP, 0)
    , selecting(sel)
  {
    set_status(STATUS_READY);
  }

  virtual void add_member(MediaContainer* txt)  {
    if (vec.size() >= 3) return;
    vec.push_back(txt);
  }
  virtual MediaContainer* get_option(uint8_t id) const {
    if (id > vec.size()) return nullptr;
    return vec[id];
  }
  virtual uint8_t get_selected_id() const {
    return selecting;
  }
};

} // namespace dice

#endif