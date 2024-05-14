#include "imageh/rgb565_umlogo.h"
#include "imageh/rgb565_author.h"
#include "imageh/rgb565_barman.h"
#include "imageh/rgb565_movie_star.h"
#include "imageh/rgb565_psychic.h"
#include "imageh/rgb565_queen.h"
#include "imageh/rgb565_sergeant.h"
// #include "imageh/rgb565_text_1.h"
// #include "imageh/rgb565_text_2.h"
// #include "imageh/rgb565_text_3.h"
// #include "imageh/rgb565_text_4.h"
// #include "imageh/rgb565_text_5.h"
// #include "imageh/rgb565_text_6.h"
// #include "imageh/rgb565_text_7.h"
// #include "imageh/rgb565_text_8.h"

#define USING_SPI true

// Testing flag, DELETE AFTERWARDS
bool colored = true;


uint16_t img_index = 0;
const uint16_t MAX_IMG_INDEX = 1;
const uint16_t * imgs[MAX_IMG_INDEX] = {umlogo};
            // {umlogo,author,barman};
            // {movie_star,psychic,queen};
            // {sergeant,text_1,text_2};
            // text_3,text_4,text_5,
            // {text_6,text_7,text_8};

#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_FT6206.h>
#include <Adafruit_CST8XX.h>

#define MODE_IMG 0;
#define MODE_ANIM 1;
#define MODE_TXT 2;
uint8_t mode = MODE_IMG;

// SETUP
Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
    PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI,
    &Wire, 0x3F);
    
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
    TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
    TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
    TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,
    1 /* hsync_polarity */, 50 /* hsync_front_porch */, 2 /* hsync_pulse_width */, 44 /* hsync_back_porch */,
    1 /* vsync_polarity */, 16 /* vsync_front_porch */, 2 /* vsync_pulse_width */, 18 /* vsync_back_porch */
    );

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
// 4.0" 480x480 rectangle bar display
   480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
   expander, GFX_NOT_DEFINED /* RST */, tl040wvs03_init_operations, sizeof(tl040wvs03_init_operations));

uint16_t * screen_buffer;




#include <ESP32DMASPISlave.h>
ESP32DMASPI::Slave slave;
static constexpr size_t SPI_MOSI_BUFFER_SIZE = 4092;  // 4088 Bytes content (4k), last 4 bytes are ignored due to driver bug
/* --
 - Protocol: 1 byte for device address, 2nd byte for message type, 3rd-4th bytes for message lengths, 5th- 4092-th bytes content
    - DUE TO DRIVER BUG, SENDER SHOULD SEND 4 BYTES MORE, SO 4096 BYTES
    - message types: 1 for ping; 3 for draw text, 7 for sending image; 65535 for stopping transaction
-- */

static constexpr size_t SPI_MISO_BUFFER_SIZE = 64;
static constexpr size_t QUEUE_SIZE = 1;
uint8_t* dma_tx_buf;
uint8_t* dma_rx_buf;
size_t last_update_time;

void setup(void)
{
  // Init serial
  Serial.begin(115200);
  Serial.println("Beginning");

  // Check PSRAM Init Status
  if (psramInit()) Serial.println("\nPSRAM correctly initialized");
  else Serial.println("PSRAM not available");

  screen_buffer = (uint16_t *) ps_malloc(gfx->width() * gfx->height() * sizeof(uint16_t));

#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif
#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  if (!gfx->begin()) Serial.println("gfx->begin() failed!");
  Serial.println("GFX Initialized!");

  Wire.setClock(1000000); // speed up I2C 

  gfx->fillScreen(BLACK);
  gfx->setUTF8Print(true);

  expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
  expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

  fill_from_source(screen_buffer, umlogo);
  draw_img(screen_buffer);

  Serial.println("Screen Initialized!");

#if USING_SPI
  dma_tx_buf = slave.allocDMABuffer(SPI_MISO_BUFFER_SIZE);
  dma_rx_buf = slave.allocDMABuffer(SPI_MOSI_BUFFER_SIZE);

  slave.setDataMode(SPI_MODE0);
  slave.setMaxTransferSize(SPI_MOSI_BUFFER_SIZE);
  slave.setQueueSize(QUEUE_SIZE);

  // begin() after setting
  slave.begin();  // default: HSPI (please refer README for pin assignments)

  Serial.println("SPI Initialized!");
#endif


  last_update_time = millis();
}


void loop()
{
  // if no transaction is in flight and all results are handled, queue new transactions
  if (slave.hasTransactionsCompletedAndAllResultsHandled()) {
      // do some initialization for tx_buf and rx_buf

      // queue multiple transactions
      // slave first receive some stuff
      slave.queue(NULL, dma_rx_buf, SPI_MOSI_BUFFER_SIZE);
      // then sends some stuff
      slave.queue(dma_tx_buf, NULL, SPI_MISO_BUFFER_SIZE);
  
      // finally, we should trigger transaction in the background
      slave.trigger();
  }

  
  if (colored==true && (millis()-last_update_time>1000)) {
    Serial.println("Updating Images");
    fill_from_source(screen_buffer,imgs[img_index]);
    draw_img(screen_buffer);
    colored = false;
    img_index = (img_index + 1) % MAX_IMG_INDEX;
    last_update_time = millis();
  }
  else if (colored==false && (millis()-last_update_time>1000)){
    Serial.println("Updating Texts");
    gfx->fillScreen(65535);
    // gfx->drawRect(10, 10, 300, 300, RED);

    gfx->setTextColor(RED);
    gfx->setTextSize(2);

    // Spanish
    gfx->setFont(u8g2_font_unifont_tf); 
    gfx->setCursor(40, 40);
    gfx->println("Psíquico");

    // Germen
    gfx->setFont(u8g2_font_unifont_tf); 
    gfx->setCursor(280, 40);
    gfx->println("Hellseher");

    // Russian
    gfx->setFont(u8g2_font_cu12_t_cyrillic); 
    gfx->setCursor(40, 160);
    gfx->println("экстрасенс");

    // French
    gfx->setFont(u8g2_font_unifont_tf); 
    gfx->setCursor(280, 160);
    gfx->println("Psychique");

    // English
    gfx->setFont(u8g2_font_unifont_tf); 
    gfx->setCursor(40, 280);
    gfx->println("Psychic");

    // Hindi
    gfx->setFont(u8g2_font_unifont_t_devanagari);
    gfx->setCursor(280, 280);
    gfx->println("मानसिक");

    // Chinese
    gfx->setFont(u8g2_font_unifont_t_chinese);
    gfx->setCursor(40, 400);
    gfx->println("靈媒");
    
    // Arabic
    gfx->setFont(u8g2_font_unifont_t_arabic);
    gfx->setCursor(280, 400);
    gfx->println("نفسية");

    colored = true;
    last_update_time = millis();
  }

  // use the buttons to turn off
  if (! expander->digitalRead(PCA_BUTTON_DOWN)) {
    expander->digitalWrite(PCA_TFT_BACKLIGHT, LOW);
  }
  // and on the backlight
  if (! expander->digitalRead(PCA_BUTTON_UP)) {
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
  }


  // if all transactions are completed and all results are ready, handle results
  if (slave.hasTransactionsCompletedAndAllResultsReady(QUEUE_SIZE)) {
    // get received bytes for all transactions
    const std::vector<size_t> received_bytes = slave.numBytesReceivedAll();
    // do something with received_bytes and rx_buf if needed
    Serial.println(received_bytes.size());
  }
  delay(1);
}


// Draw image
  void draw_img(uint16_t * screen_buffer) {
    gfx->draw16bitRGBBitmap(0, 0, screen_buffer, gfx->width(), gfx->height());
  }

  // Draw image from a const source
  void draw_from_source(const uint16_t * source, uint16_t * buffer) {
    fill_from_source(buffer, source);
    draw_img(buffer);
  }

  // Draw text
  // void draw_text() {
  //   if (mode != MODE_TEXT) init_for_text();
  //   Serial.println("Printed " + );
  // }


void fill_from_source(uint16_t * buffer, const uint16_t * source){
  int width = gfx->width();
  int height = gfx->height();
  
  for(int y = 0; y < height; ++y){
    for(int x = 0; x < width; ++x) {
      buffer[y*width + x] = source[y * width + x];
    }
  }
}

void fill_color(uint16_t * buffer, const uint16_t color){
  int width = gfx->width();
  int height = gfx->height();
  
  for(int y = 0; y < height; ++y){
    for(int x = 0; x < width; ++x) {
      buffer[y*width + x] = color;
    }
  }
}

