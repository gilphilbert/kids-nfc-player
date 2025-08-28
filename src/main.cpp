#include <Arduino.h>

#include "player.h"

// --------------------------------------------- GLOBALS --------------------------------------------- //
// -----------------------//
//           NFC          //
// -----------------------//
#include "PN532_SPI.h"
#include "PN532.h"
#include <NfcAdapter.h>
#include "SPI.h"

#define NDEF_DEBUG

#define NFC_IRQ     1//15
#define NFC_SS      2//16
#define NFC_CLK     5
#define NFC_MOSI    3
#define NFC_MISO    4

#define NFC_CARD_READ_GAP 2000

SPIClass nfc_spi = SPIClass(FSPI);
PN532_SPI pn532spi(nfc_spi, NFC_SS);
NfcAdapter nfc = NfcAdapter(pn532spi, NFC_IRQ);

unsigned long lastRead = 0;
int irqCurr;
int irqPrev;

// -----------------------//
//           TFT          //
// -----------------------//
#include <TFT_eSPI.h>
#include <lvgl.h>
TFT_eSPI tft = TFT_eSPI();
#define TFT_HOR_RES   320
#define TFT_VER_RES   480
#define TFT_ROTATION  LV_DISPLAY_ROTATION_270
#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];
#if LV_USE_LOG != 0
void my_print( lv_log_level_t level, const char * buf ) {
  LV_UNUSED(level);
  Serial.println(buf);
  Serial.flush();
}
#endif


void erasingCard();
void writingCard();
void showMenu();

unsigned long lastTouch;
#define SCREEN_OFF_DELAY 30  //delay in seconds

static uint32_t my_tick(void) {
  return millis();
}

#include "FT6336U.h"
#define I2C_SDA 42
#define I2C_SCL 40
#define RST_N_PIN 41
#define INT_N_PIN 45
FT6336U ft6336u(I2C_SDA, I2C_SCL, RST_N_PIN, INT_N_PIN);

unsigned long last_touches[2] = {0, 0};


void my_touchpad_read( lv_indev_t * indev, lv_indev_data_t * data ) {
  FT6336U_TouchPointType tp = ft6336u.scan();
  if (tp.touch_count > 0) {
    data->state = LV_INDEV_STATE_PRESSED;
    //need to handle rotation here
    uint32_t x = tp.tp[0].x;
    uint32_t y = tp.tp[0].y;

    if (TFT_ROTATION == LV_DISPLAY_ROTATION_90) {
      x = TFT_HOR_RES - x;
      y = TFT_VER_RES - y;
    } else if (TFT_ROTATION == LV_DISPLAY_ROTATION_270) {
      x = TFT_HOR_RES - x;
      y = TFT_VER_RES - y;
    } else {
      x = tp.tp[0].x;
      y = tp.tp[0].y;
    }
    
    data->point.x = x;
    data->point.y = y;

    digitalWrite(TFT_BL, HIGH); //someone touched the display! turn the display on

    if (!Player::isPlaying()) {
      lastTouch = millis(); //need this to turn the display off
    }

    if (last_touches[1] > 0 && millis() - last_touches[1] < 500) {
      //triple-tap!
      Serial.println("Triple-tap");
      if (!Player::isPlaying())
        //displayFileList();
        showMenu();
    }
    last_touches[1] = last_touches[0];
    last_touches[0] = millis();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

lv_color_t blue = lv_color_hex(0x2364AA);
lv_color_t white = lv_color_hex(0xFFFFFF);
lv_color_t yellow = lv_color_hex(0xDEB841);
lv_color_t green = lv_color_hex(0x4BB07D);
lv_color_t gray = lv_color_hex(0xEAECEE);
lv_color_t black = lv_color_hex(0x2B2D42);
lv_color_t dark_gray = lv_color_hex(0xD7D8DD);
lv_color_t light_blue = lv_color_hex(0x477DB9);
lv_color_t orange = lv_color_hex(0xFF5F24);
lv_color_t red = lv_color_hex(0xD84347);

extern const lv_image_dsc_t img_nfc_card_logo;
extern const lv_image_dsc_t img_play;
extern const lv_image_dsc_t img_pause;
extern const lv_image_dsc_t img_stop;
extern const lv_image_dsc_t micro_sd;
extern const lv_image_dsc_t nfc_card;
extern const lv_font_t fnt_sora_medium_16;
extern const lv_font_t fnt_sora_medium_18;
extern const lv_font_t fnt_sora_medium_22;

lv_obj_t * progressBar;

uint8_t elapsed;
unsigned long nextElapsedTick;

bool writing = false;
lv_obj_t * mainFileList;
String fileToWrite;

// --------------------------------------------- FUNCTIONS --------------------------------------------- //
// -----------------------//
//           NFC          //
// -----------------------//
void startListening() {
  //Serial.println("Listening");
  irqPrev = irqCurr = HIGH;
  nfc.startPassive();
}

String readCard() {
  Serial.print("Reading card:");
  String retStr = "";
  if (nfc.tagPresent()) {
    NfcTag tag = nfc.read();
    if(!tag.hasNdefMessage()) {
      return "";
    }
    NdefMessage message = tag.getNdefMessage();
    if (message.getRecordCount() > 0) {
      NdefRecord record = message.getRecord(0);
      int payloadLength = record.getPayloadLength();
      byte payload[payloadLength];
      record.getPayload(payload);
      String payloadAsString = "";
      for (int c = 3; c < payloadLength; c++) {
        payloadAsString += (char)payload[c];
      }
      retStr = payloadAsString;
    }
  }
  Serial.println(retStr);
  return retStr;
}

bool writeCard() {
  bool success = false;
  if (nfc.tagPresent()) {
    Serial.println("Card present");
    Serial.println("Erasing card");
    erasingCard();
    success = nfc.erase();
    if (success) {
      Serial.println("Erased card. Writing card");
      writingCard();
      NdefMessage message = NdefMessage();
      message.addTextRecord(fileToWrite);
      success = nfc.write(message);
    }
  }
  if (success)
    Serial.println("Card written");
  else
    Serial.println("Card write failed");
  return success;
}

// -----------------------//
//          UPDATE        //
// -----------------------//
#include <update.h>
//#include "update.h"

// -----------------------//
//           TFT          //
// -----------------------//
void scanCard() {
  lv_obj_t * container = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(container, blue, LV_PART_MAIN);

  lv_obj_t * card = lv_obj_create(container);
  lv_obj_set_width(card, 170);
  lv_obj_set_height(card, 110);
  lv_obj_set_style_bg_color(card, yellow, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *logo = lv_image_create(card);
  lv_image_set_src(logo, &img_nfc_card_logo);
  lv_obj_align_to(logo, card, LV_ALIGN_CENTER, 30, 0);

  lv_obj_t *label = lv_label_create( container );
  lv_label_set_text( label, "Scan a card" );
  lv_obj_set_style_text_font(label, &fnt_sora_medium_18, LV_PART_MAIN);
  lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );
  lv_obj_set_style_text_color(label, white, LV_PART_MAIN);
  lv_obj_align_to(label, card, LV_ALIGN_BOTTOM_MID, 0, 50);

  //lv_screen_load_anim(container, LV_SCR_LOAD_ANIM_OVER_TOP, 200, 0, true);
  lv_screen_load(container);
}

void cardError() {
  lv_obj_t * container = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(container, red, LV_PART_MAIN);

  lv_obj_t *logo = lv_image_create(container);
  lv_image_set_src(logo, &micro_sd);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *label = lv_label_create( container );
  lv_label_set_text( label, "No SD Card or Card Damaged" );
  lv_obj_set_style_text_font(label, &fnt_sora_medium_18, LV_PART_MAIN);
  lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );
  lv_obj_set_style_text_color(label, white, LV_PART_MAIN);
  lv_obj_align_to(label, logo, LV_ALIGN_BOTTOM_MID, 0, 50);

  lv_screen_load(container);
}

static void stopPressed(lv_event_t * e) {
  lv_event_code_t code = lv_event_get_code(e);
  if(code == LV_EVENT_CLICKED) {
    Player::stop();
  }
}

static void pausePressed(lv_event_t * e) {
  Player::pause();
}

void showSong(audioMetadata _md) {
  lv_obj_t * container = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(container, blue, LV_PART_MAIN);

  lv_obj_t * artbg = lv_obj_create(container);
  lv_obj_set_width(artbg, 120);
  lv_obj_set_height(artbg, 120);
  lv_obj_set_style_radius(artbg, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(artbg, light_blue, LV_PART_MAIN);
  lv_obj_set_style_border_width(artbg, 0, LV_PART_MAIN);
  lv_obj_align(artbg, LV_ALIGN_TOP_LEFT, 25, 50);

  //actual art image needs to go here

  lv_obj_t *title = lv_label_create( container );
  lv_label_set_text( title, _md.title.c_str() );
  lv_obj_set_style_text_font(title, &fnt_sora_medium_22, LV_PART_MAIN);
  lv_obj_align( title, LV_ALIGN_CENTER, 0, 0 );
  lv_obj_set_style_text_color(title, white, LV_PART_MAIN);
  lv_obj_align_to(title, artbg, LV_ALIGN_OUT_RIGHT_TOP, 22, 20);

  lv_obj_t * artist = lv_label_create( container );
  lv_label_set_text( artist, _md.artist.c_str() );
  lv_obj_set_style_text_font(artist, &fnt_sora_medium_18, LV_PART_MAIN);
  lv_obj_align( artist, LV_ALIGN_CENTER, 0, 0 );
  lv_obj_set_style_text_color(artist, white, LV_PART_MAIN);
  lv_obj_align_to(artist, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 15);

  progressBar = lv_bar_create(container);
  lv_obj_set_width(progressBar, 277);
  lv_obj_set_height(progressBar, 8);
  lv_obj_set_style_radius(progressBar, 8, LV_PART_MAIN);

  lv_obj_set_style_bg_color(progressBar, light_blue, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(progressBar, 255, LV_PART_MAIN);
  lv_obj_set_style_bg_color(progressBar, yellow, LV_PART_INDICATOR);

  lv_obj_align_to(progressBar, artbg, LV_ALIGN_OUT_RIGHT_BOTTOM, 25, -15);
  lv_bar_set_range(progressBar, 0, _md.duration);
  lv_bar_set_value(progressBar, 0, false);

  lv_obj_t * play_button = lv_obj_create(container);
  lv_obj_set_width(play_button, 60);
  lv_obj_set_height(play_button, 60);
  lv_obj_set_style_radius(play_button, 50, LV_PART_MAIN);
  lv_obj_set_style_bg_color(play_button, yellow, LV_PART_MAIN);
  lv_obj_set_style_border_width(play_button, 0, LV_PART_MAIN);
  lv_obj_align(play_button, LV_ALIGN_TOP_MID, 0, 215);

  // button event handler goes here

  lv_obj_t * play_button_image = lv_image_create(play_button);
  lv_image_set_src(play_button_image, &img_play);
  lv_obj_set_width(play_button_image, 18);
  lv_obj_set_height(play_button_image, 18);
  lv_obj_align_to(play_button_image, play_button, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t * pause_button = lv_obj_create(container);
  lv_obj_set_width(pause_button, 60);
  lv_obj_set_height(pause_button, 60);
  lv_obj_set_style_radius(pause_button, 50, LV_PART_MAIN);
  lv_obj_set_style_bg_color(pause_button, light_blue, LV_PART_MAIN);
  lv_obj_set_style_border_width(pause_button, 0, LV_PART_MAIN);
  lv_obj_align_to(pause_button, play_button, LV_ALIGN_OUT_LEFT_MID, -60, 0);
  lv_obj_add_event_cb(pause_button, pausePressed, LV_EVENT_CLICKED, NULL);

  lv_obj_t * pause_button_image = lv_image_create(pause_button);
  lv_image_set_src(pause_button_image, &img_pause);
  lv_obj_set_width(pause_button_image, 18);
  lv_obj_set_height(pause_button_image, 18);
  lv_obj_align_to(pause_button_image, pause_button, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(pause_button_image, pausePressed, LV_EVENT_CLICKED, NULL);

  lv_obj_t * stop_button = lv_button_create(container);
  lv_obj_set_width(stop_button, 60);
  lv_obj_set_height(stop_button, 60);
  lv_obj_set_style_radius(stop_button, 50, LV_PART_MAIN);
  lv_obj_set_style_bg_color(stop_button, light_blue, LV_PART_MAIN);
  lv_obj_set_style_border_width(stop_button, 0, LV_PART_MAIN);
  lv_obj_align_to(stop_button, play_button, LV_ALIGN_OUT_RIGHT_MID, 60, 0);
  lv_obj_add_event_cb(stop_button, stopPressed, LV_EVENT_ALL, NULL);

  lv_obj_t * stop_button_image = lv_image_create(stop_button);
  lv_image_set_src(stop_button_image, &img_stop);
  lv_obj_set_width(stop_button_image, 18);
  lv_obj_set_height(stop_button_image, 18);
  lv_obj_align_to(stop_button_image, stop_button, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(stop_button_image, stopPressed, LV_EVENT_CLICKED, NULL);

  lv_screen_load(container);
}

void showUpdateScreen() {
  lv_obj_t * container = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(container, blue, LV_PART_MAIN);

  //borrow the existing handle for progressBar, since we won't be using it for playback!
  progressBar = lv_bar_create(container);
  lv_obj_set_width(progressBar, 240);
  lv_obj_set_height(progressBar, 8);
  lv_obj_set_style_radius(progressBar, 8, LV_PART_MAIN);

  lv_obj_set_style_bg_color(progressBar, light_blue, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(progressBar, 255, LV_PART_MAIN);
  lv_obj_set_style_bg_color(progressBar, yellow, LV_PART_INDICATOR);

  lv_obj_align(progressBar, LV_ALIGN_CENTER, 0, -15);
  lv_bar_set_range(progressBar, 0, 100);
  lv_bar_set_value(progressBar, 0, false);

  lv_obj_t * label = lv_label_create(container);
  lv_label_set_text(label, "Updating, firmware, this will only take a moment");
  lv_obj_set_style_text_font(label, &fnt_sora_medium_16, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, white, LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 15);

  lv_screen_load(container);
  lv_timer_handler();
}

void updateProgress(size_t current, size_t total) {
  lv_bar_set_range(progressBar, 0, total);
  lv_bar_set_value(progressBar, current, false);
  lv_timer_handler();
  delay(2);
}

void erasingCard() {
  lv_obj_t * container = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(container, yellow, LV_PART_MAIN);

  lv_obj_t *logo = lv_image_create(container);
  lv_image_set_src(logo, &nfc_card);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *label = lv_label_create( container );
  lv_label_set_text( label, "Erasing card. Keep card at reader!" );
  lv_obj_set_style_text_font(label, &fnt_sora_medium_18, LV_PART_MAIN);
  lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );
  lv_obj_set_style_text_color(label, white, LV_PART_MAIN);
  lv_obj_align_to(label, logo, LV_ALIGN_BOTTOM_MID, 0, 50);

  lv_screen_load(container);
}

void writingCard() {
  lv_obj_t * container = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(container, yellow, LV_PART_MAIN);

  lv_obj_t *logo = lv_image_create(container);
  lv_image_set_src(logo, &nfc_card);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *label = lv_label_create( container );
  lv_label_set_text( label, "Writing card. Keep card at reader!" );
  lv_obj_set_style_text_font(label, &fnt_sora_medium_18, LV_PART_MAIN);
  lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );
  lv_obj_set_style_text_color(label, white, LV_PART_MAIN);
  lv_obj_align_to(label, logo, LV_ALIGN_BOTTOM_MID, 0, 50);

  lv_screen_load(container);
}

void cardWriteSuccess() {

  lv_obj_t * container = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(container, green, LV_PART_MAIN);

  lv_obj_t *logo = lv_image_create(container);
  lv_image_set_src(logo, &nfc_card);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *label = lv_label_create( container );
  lv_label_set_text( label, "Card written succesfully" );
  lv_obj_set_style_text_font(label, &fnt_sora_medium_18, LV_PART_MAIN);
  lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );
  lv_obj_set_style_text_color(label, white, LV_PART_MAIN);
  lv_obj_align_to(label, logo, LV_ALIGN_BOTTOM_MID, 0, 50);

  lv_screen_load(container);
}

static void cancel_write(lv_event_t * e) {
  scanCard();
  writing = false;
  fileToWrite = "";
}

static void fileSelected(lv_event_t * e) {
  lv_obj_t * ta = (lv_obj_t *)lv_event_get_target(e);
  const char *te = lv_label_get_text(ta);

  lv_obj_t * container = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(container, blue, LV_PART_MAIN);

  lv_obj_t *logo = lv_image_create(container);
  lv_image_set_src(logo, &nfc_card);
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -40);

  lv_obj_t *label = lv_label_create( container );
  lv_label_set_text( label, "Present card to write" );
  lv_obj_set_style_text_font(label, &fnt_sora_medium_18, LV_PART_MAIN);
  lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );
  lv_obj_set_style_text_color(label, white, LV_PART_MAIN);
  lv_obj_align_to(label, logo, LV_ALIGN_BOTTOM_MID, 0, 50);

  lv_obj_t * cancel_button = lv_button_create(container);
  lv_obj_set_style_bg_color(cancel_button, yellow, LV_PART_MAIN);
  lv_obj_align(cancel_button, LV_ALIGN_BOTTOM_MID, 0, -30);

  lv_obj_t * button_text = lv_label_create(cancel_button);
  lv_obj_set_style_text_color(button_text, white, LV_PART_MAIN);
  lv_label_set_text(button_text, "Cancel");
  lv_obj_add_event_cb(cancel_button, cancel_write, LV_EVENT_CLICKED, NULL);
  

  mainFileList = NULL;

  writing = true;
  fileToWrite = te;

  lv_screen_load(container);
}


static void back_event_handler(lv_event_t * e) {
  lv_obj_t * obj = lv_event_get_target_obj(e);
  lv_obj_t * menu = (lv_obj_t *)lv_event_get_user_data(e);
  if (lv_menu_back_button_is_root(menu, obj)) {
    scanCard();
  }
}

typedef enum {
    LV_MENU_ITEM_BUILDER_VARIANT_1,
    LV_MENU_ITEM_BUILDER_VARIANT_2
} lv_menu_builder_variant_t;

static lv_obj_t * create_text(lv_obj_t * parent, const char * icon, const char * txt, lv_menu_builder_variant_t builder_variant) {
    lv_obj_t * obj = lv_menu_cont_create(parent);

    lv_obj_t * img = NULL;
    lv_obj_t * label = NULL;

    if(icon) {
        img = lv_image_create(obj);
        lv_image_set_src(img, icon);
    }

    if(txt) {
        label = lv_label_create(obj);
        lv_label_set_text(label, txt);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_flex_grow(label, 1);
    }

    if(builder_variant == LV_MENU_ITEM_BUILDER_VARIANT_2 && icon && txt) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
        lv_obj_swap(img, label);
    }

    return obj;
}

static lv_obj_t * create_slider(lv_obj_t * parent, const char * icon, const char * txt, int32_t min, int32_t max, int32_t val) {
    lv_obj_t * obj = create_text(parent, icon, txt, LV_MENU_ITEM_BUILDER_VARIANT_2);

    lv_obj_t * slider = lv_slider_create(obj);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, min, max);
    lv_slider_set_value(slider, val, false);

    if(icon == NULL) {
        lv_obj_add_flag(slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }

    return obj;
}

lv_obj_t * menu_container;
lv_obj_t * files_menu_page;

void showMenu() {
  lv_obj_t * container = lv_obj_create(NULL);
  lv_obj_t * menu = lv_menu_create(container);

  //size menu
  lv_obj_set_size(menu, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL));
  lv_obj_center(menu);

  //edit header to add back button
  lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_ENABLED);
  lv_obj_add_event_cb(menu, back_event_handler, LV_EVENT_CLICKED, menu);

  //
  lv_obj_t * cont;
  lv_obj_t * label;

  lv_obj_t * main_page = lv_menu_page_create(menu, NULL);

  //files menu
  files_menu_page = lv_menu_page_create(menu, "Files on SD card");
  cont = lv_menu_cont_create(files_menu_page);

  std::vector<String> files = Player::getAllFiles();
  for (int i = 0; i < files.size(); i++) {
    int filePos = i;
    label = lv_label_create(cont);
    lv_label_set_text(label, files.at(i).c_str());
    lv_obj_add_event_cb(label, fileSelected, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    //some sort of callback goes here
  }

  //system settings menu
  lv_obj_t * system_settings_page = lv_menu_page_create(menu, "System settings");
  cont = lv_menu_cont_create(system_settings_page);
  create_slider(cont, LV_SYMBOL_VOLUME_MID, "Volume", 0, 21, 16);

  //main page items
  cont = lv_menu_cont_create(main_page);
  label = lv_label_create(cont);
  lv_label_set_text(label, "Write a new card");
  lv_menu_set_load_page_event(menu, cont, files_menu_page);

  cont = lv_menu_cont_create(main_page);
  label = lv_label_create(cont);
  lv_label_set_text(label, "System settings");
  lv_menu_set_load_page_event(menu, cont, system_settings_page);

  lv_menu_set_page(menu, main_page);

  //menu_container = container;
  lv_screen_load(container);

  
}

void setupDisplay() {
  lv_init();
  lv_tick_set_cb(my_tick);
  #if LV_USE_LOG != 0
      lv_log_register_print_cb( my_print );
  #endif
  lv_display_t * disp;
  disp = lv_tft_espi_create(TFT_HOR_RES, TFT_VER_RES, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, TFT_ROTATION);

  ft6336u.begin();
  
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*ouchpad should have POINTER type*/
  lv_indev_set_read_cb(indev, my_touchpad_read);

  lastTouch = millis();

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
}


// -----------------------//
//          AUDIO         //
// -----------------------//
void trackMetadataHandler(audioMetadata md) {
  //Serial.print("MAIN::CallBack::");
  //Serial.println(md.title);
  showSong(md);
  //anything else we need to do
}

void playStateChanged(PlayState ps) {
  if (ps == PLAYING) {
    elapsed = 0;
    nextElapsedTick = millis() + 1000;
    lastTouch = 0;
    digitalWrite(TFT_BL, HIGH); //turn the display on
  }
  if (ps == STOPPED) {
    //Serial.println("MAIN::Player reports stopped");
    nextElapsedTick = 0;
    elapsed = 0;
    lastTouch = millis(); //need this to turn the display off
    scanCard();
  }
}


// -----------------------//
//          CORE         //
// -----------------------//
void setup() {
  Serial.begin(115200);
  Serial.println("Starting");
  
  //NFC Setup
  nfc_spi.begin(NFC_CLK, NFC_MISO, NFC_MOSI, NFC_SS);
  nfc.begin();

  bool cardFound = Player::init(); //if this returns false, the SD card couldn't be opened!
  Player::registerMDCallback(trackMetadataHandler);
  Player::registerStateCallback(playStateChanged);

  //delay(1000);

  setupDisplay();
  elapsed = 0;
  nextElapsedTick = 0;

  if (!cardFound) {
    cardError();
    lv_timer_handler();
    while (!cardFound) {
      delay(3000);
      cardFound = Player::init();
    }
  }

  if (SDUpdate::check()) {
    //create update screen
    showUpdateScreen();
    //register progress update (kinda cheating here)
    Update.onProgress(updateProgress);
    //perform the update
    SDUpdate::update();
  } else {
    Serial.println("No firmware update available");
  }

  scanCard();
  
  startListening();
  Serial.println("NFC Startup complete");
}

void loop() {
  irqCurr = digitalRead(NFC_IRQ);
  if (irqCurr == LOW && irqPrev == HIGH) {
    Serial.println("Card IRQ triggered");
    if (!writing) {
      String str = readCard();
      if (str != "" && str.endsWith(".mp3")) {
        Serial.println("Requesting playback");
        if (Player::play(str)) {
          //showSong(str.c_str());
          //do some stuff? not sure there's much to do. Maybe start counting an elapsed timer? We'll need that!
        }
      }
    } else {
      //this is where we write the card
      Serial.println("Attempting to write card");
      if (writeCard()) {
        Serial.println("Card write success");
        fileToWrite = "";
        writing = false;
        //show a screen about writing successful
        cardWriteSuccess();
        lv_task_handler();
        delay(2000); //wait so the user can read the screen
        scanCard(); //get ready to read the next card
      } else {
        Serial.println("Card write failed");
      }
    }
    lastRead = millis();
  }
  irqPrev = irqCurr;

  if (lastRead > 0 && lastRead + NFC_CARD_READ_GAP < millis()) {
    startListening();
    lastRead = 0;
  }

  if (nextElapsedTick > 0 && millis() > nextElapsedTick) {
    elapsed++;
    nextElapsedTick += 1000;
    lv_bar_set_value(progressBar, elapsed, false);
  }

  lv_timer_handler();

  if (lastTouch && millis() > lastTouch + (SCREEN_OFF_DELAY * 1000)) {
    //turn off the screen
    digitalWrite(TFT_BL, LOW);

    writing = 0;
    scanCard();
    fileToWrite = "";

    lastTouch = 0;
  }

}
