#define LGFX_USE_V1
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <Wire.h>
#include "ff.h"

// ==================== ЭКСПОРТ SquareLine ====================
// Подключаем экспортированные UI файлы
#include "ui.h"

// ======================= СЕТЬ И ВЕНТИЛЯТОР =======================

#define WIFI_SSID     "1921198"
#define WIFI_PASSWORD "Layzveb'sDevices"
#define FAN_CTRL_IP   "192.168.1.90"
#define FAN_CTRL_PORT 80

// ======================= I2C и дисплей =====================

#define I2C_SDA 4
#define I2C_SCL 5
#define PI4IO_I2C_ADDR 0x43

// ======================= LVGL БУФЕР (ОПТИМИЗИРОВАНО) ===========

#define buf_size 40
lv_img_dsc_t *fan_dsc = nullptr;

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

public:
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read  = 20000000;
      cfg.spi_3wire  = true;
      cfg.use_lock   = true;
      cfg.dma_channel= SPI_DMA_CH_AUTO;
      cfg.pin_sclk   = 6;
      cfg.pin_mosi   = 7;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs         = 10;
      cfg.pin_rst        = -1;
      cfg.pin_busy       = -1;
      cfg.memory_width   = 240;
      cfg.memory_height  = 240;
      cfg.panel_width    = 240;
      cfg.panel_height   = 240;
      cfg.offset_x       = 0;
      cfg.offset_y       = 0;
      cfg.offset_rotation= 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable       = false;
      cfg.invert         = true;
      cfg.rgb_order      = false;
      cfg.dlen_16bit     = false;
      cfg.bus_shared     = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;

static const uint32_t screenWidth = 240;
static const uint32_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[2][screenWidth * buf_size];

static lv_indev_t * indev_touch;

bool touch_driver_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data) {
    uint16_t tx, ty;
    bool touched = tft.getTouch(&tx, &ty);   // <= именно так

    if (touched) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    return false;
}


// ======================= СТРУКТУРА СОСТОЯНИЯ ====================

struct VentState {
  float    temperature;
  float    humidity;
  bool     ventilator_on;
  uint8_t  mode;
  uint32_t time_left_sec;
  uint16_t manual_time_minutes;
  uint8_t  humidity_threshold;
  bool     valid;
};

VentState ventState = {
  .temperature          = 0.0f,
  .humidity             = 0.0f,
  .ventilator_on        = false,
  .mode                 = 0,
  .time_left_sec        = 0,
  .manual_time_minutes  = 15,
  .humidity_threshold   = 75,
  .valid                = false
};

bool     wifiConnected  = false;
uint32_t lastStatePoll  = 0;
uint32_t lastWiFiRetry  = 0;

#define STATE_POLL_MS 2000
#define WIFI_RETRY_MS 10000

// ======================= СВАЙП И ЭКРАНЫ =======================

uint8_t  current_screen  = 0;  // 0=main, 1=time, 2=humidity
int32_t  swipe_x_start   = 0;
bool     swipe_active    = false;

// Переменные для вращения вентилятора
uint32_t fan_rotation_angle = 0;
uint32_t last_fan_update    = 0;

// ======================= HTTP ФУНКЦИИ =======================

bool httpGET(const String &path, String &response) {
  if (!wifiConnected) return false;

  HTTPClient http;
  http.setTimeout(3000);
  String url = "http://" + String(FAN_CTRL_IP) + ":" + String(FAN_CTRL_PORT) + path;
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();

  if (code == HTTP_CODE_OK) {
    response = http.getString();
    http.end();
    return true;
  }
  http.end();
  return false;
}

bool httpGETSimple(const String &path) {
  String resp;
  return httpGET(path, resp);
}

bool httpGETWithParam(const String &path, const String &param, const String &value) {
  String fullPath = path + "?" + param + "=" + value;
  return httpGETSimple(fullPath);
}

// ======================= JSON ПАРСИНГ =======================

float extractFloat(const String &json, const char* key) {
  int idx = json.indexOf(key);
  if (idx < 0) return 0.0f;
  idx = json.indexOf(':', idx);
  if (idx < 0) return 0.0f;
  int end = json.indexOf(',', idx + 1);
  if (end < 0) end = json.indexOf('}', idx + 1);
  if (end < 0) return 0.0f;
  String val = json.substring(idx + 1, end);
  val.trim();
  return val.toFloat();
}

uint32_t extractUInt(const String &json, const char* key) {
  int idx = json.indexOf(key);
  if (idx < 0) return 0;
  idx = json.indexOf(':', idx);
  if (idx < 0) return 0;
  int end = json.indexOf(',', idx + 1);
  if (end < 0) end = json.indexOf('}', idx + 1);
  if (end < 0) return 0;
  String val = json.substring(idx + 1, end);
  val.trim();
  return (uint32_t)val.toInt();
}

void parseStateJson(const String &json) {
  ventState.valid = false;
  if (json.length() < 10) return;

  ventState.temperature         = extractFloat(json, "temperature");
  ventState.humidity            = extractFloat(json, "humidity");
  ventState.ventilator_on       = (extractUInt(json, "ventilator") != 0);
  ventState.mode                = (uint8_t)extractUInt(json, "mode");
  ventState.time_left_sec       = extractUInt(json, "timeLeft");
  ventState.manual_time_minutes = (uint16_t)extractUInt(json, "manual_time_minutes");
  ventState.humidity_threshold  = (uint8_t)extractUInt(json, "humidity_threshold");

  ventState.valid = true;
}

void fetchVentState() {
  String resp;
  if (!httpGET("/state", resp)) {
    ventState.valid = false;
    return;
  }
  parseStateJson(resp);
}

// ======================= IO-РАСШИРИТЕЛЬ =======================

void init_IO_extender() {
  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x01);
  Wire.endTransmission();
  Wire.requestFrom(PI4IO_I2C_ADDR, 1);
  uint8_t rxdata = Wire.read();
  Serial.print("Device ID: ");
  Serial.println(rxdata, HEX);

  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x03);
  Wire.write((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4));
  Wire.endTransmission();

  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x07);
  Wire.write(~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4)));
  Wire.endTransmission();
}

void set_pin_io(uint8_t pin_number, bool value) {
  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x05);
  Wire.endTransmission();
  Wire.requestFrom(PI4IO_I2C_ADDR, 1);
  uint8_t rxdata = Wire.read();

  Wire.beginTransmission(PI4IO_I2C_ADDR);
  Wire.write(0x05);
  if (!value)
    Wire.write((~(1 << pin_number)) & rxdata);
  else
    Wire.write((1 << pin_number) | rxdata);
  Wire.endTransmission();
}

// ======================= LVGL: FLUSH / TOUCH =====================

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  if (tft.getStartCount() == 0) {
    tft.endWrite();
  }

  tft.pushImageDMA(area->x1, area->y1,
                   area->x2 - area->x1 + 1,
                   area->y2 - area->y1 + 1,
                   (lgfx::swap565_t *)&color_p->full);

  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  data->state = LV_INDEV_STATE_REL;
}

// ======================= WIFI =======================

void connectWiFi() {
  Serial.println("\nConnecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi failed!");
  }
}

// ======================= ОБНОВЛЕНИЕ UI ========================

void update_main_screen_labels() {
  if (!ventState.valid) return;

  char buf[32];

  if (ui_LabelTempValue != NULL) {
    snprintf(buf, sizeof(buf), "%.1f°C", ventState.temperature);
    lv_label_set_text(ui_LabelTempValue, buf);
  }

  if (ui_LabelHumidValue != NULL) {
    snprintf(buf, sizeof(buf), "%.1f%%", ventState.humidity);
    lv_label_set_text(ui_LabelHumidValue, buf);
  }
}

void update_time_slider_value() {
  if (ui_LabelSetTimeValue != NULL) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u мин", ventState.manual_time_minutes);
    lv_label_set_text(ui_LabelSetTimeValue, buf);
  }

  if (ui_ArcTime != NULL) {
    lv_arc_set_value(ui_ArcTime, ventState.manual_time_minutes);
  }
}

void update_humid_slider_value() {
  if (ui_LabelSetHumidValue != NULL) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u%%", ventState.humidity_threshold);
    lv_label_set_text(ui_LabelSetHumidValue, buf);
  }

  if (ui_ArcHumid != NULL) {
    lv_arc_set_value(ui_ArcHumid, ventState.humidity_threshold);
  }
}

// ======================= ВРАЩЕНИЕ ВЕНТИЛЯТОРА ==================

void update_fan_rotation() {
  if (!ventState.ventilator_on) {
    if (ui_ImageFan != NULL) {
      lv_img_set_angle(ui_ImageFan, 0);
    }
    return;
  }

  uint32_t now = millis();
  if (now - last_fan_update > 50) {
    last_fan_update = now;
    fan_rotation_angle += 12;
    if (fan_rotation_angle >= 3600) {
      fan_rotation_angle = 0;
    }
    if (ui_ImageFan != NULL) {
      lv_img_set_angle(ui_ImageFan, fan_rotation_angle / 10);
    }
  }
}

// ======================= ОБРАБОТКА СВАЙПА =======================

void handle_screen_swipe(lv_dir_t dir) {
  if (dir == LV_DIR_LEFT) {
    current_screen = (current_screen + 1) % 3;
    Serial.printf("Swipe LEFT -> Screen %d\n", current_screen);
  } else if (dir == LV_DIR_RIGHT) {
    current_screen = (current_screen + 2) % 3;
    Serial.printf("Swipe RIGHT -> Screen %d\n", current_screen);
  }
}

// ======================= SETUP ===================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== SquareLine LVGL Ventilation Controller v3.1 ===\n");

    Wire.begin(I2C_SDA, I2C_SCL);

    // --- СНАЧАЛА LVGL ---
    lv_init();
    lv_png_init();

    // --- буфер и дисплей LVGL ---
    lv_disp_draw_buf_init(&draw_buf, buf[0], buf[1], screenWidth * buf_size);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = screenWidth;
    disp_drv.ver_res  = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_t * disp  = lv_disp_drv_register(&disp_drv);

    // --- ТАЧ КАК LVGL INPUT ---
    static lv_indev_drv_t indev_drv_touch;
    lv_indev_drv_init(&indev_drv_touch);
    indev_drv_touch.type    = LV_INDEV_TYPE_POINTER;
    indev_drv_touch.read_cb = my_touchpad_read;   // сюда потом подставишь настоящий touch_driver_read
    lv_indev_t * indev_touch = lv_indev_drv_register(&indev_drv_touch);

    // --- железо, TFT, WiFi ---
    init_IO_extender();
    delay(100);
    set_pin_io(3, true);
    set_pin_io(4, true);
    set_pin_io(2, true);

    tft.init();
    tft.initDMA();
    tft.startWrite();
    tft.fillScreen(TFT_BLACK);
    delay(200);

    connectWiFi();

    // --- UI от SquareLine (ОДИН раз, В САМ КОНЕЦ!) ---
    ui_init();
    Serial.println("LVGL initialized with SquareLine UI");

    if (wifiConnected) {
        fetchVentState();
        update_main_screen_labels();
        update_time_slider_value();
        update_humid_slider_value();
    }

    Serial.println("Setup complete!\n");
}

// ======================= LOOP ====================================

void loop()
{
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    wifiConnected = false;
    Serial.println("WiFi disconnected!");
  }

  if (!wifiConnected) {
    if (millis() - lastWiFiRetry > WIFI_RETRY_MS) {
      lastWiFiRetry = millis();
      connectWiFi();
    }
  }

  if (wifiConnected && millis() - lastStatePoll > STATE_POLL_MS) {
    lastStatePoll = millis();
    fetchVentState();
    if (ventState.valid) {
      update_main_screen_labels();
      update_time_slider_value();
      update_humid_slider_value();
    }
  }

  update_fan_rotation();

  lv_timer_handler();
  delay(5);
}
