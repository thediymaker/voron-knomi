#include "display.h"
#include <Arduino.h>
#include <Wire.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "board.h"
#include "log.h"
#include "lvgl.h"
#include "ui.h"

namespace {

class Panel : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 panel;
    lgfx::Bus_SPI bus;

public:
    Panel() {
        auto b = bus.config();
        b.spi_host = SPI2_HOST;
        b.spi_mode = 0;
        b.freq_write = 80000000;
        b.freq_read = 16000000;
        b.spi_3wire = true;
        b.use_lock = true;
        b.dma_channel = SPI_DMA_CH_AUTO;
        b.pin_sclk = PIN_LCD_SCLK;
        b.pin_mosi = PIN_LCD_MOSI;
        b.pin_miso = -1;
        b.pin_dc = PIN_LCD_DC;
        bus.config(b);
        panel.setBus(&bus);
        auto p = panel.config();
        p.pin_cs = PIN_LCD_CS;
        p.pin_rst = PIN_LCD_RST;
        p.pin_busy = -1;
        p.panel_width = p.memory_width = 240;
        p.panel_height = p.memory_height = 240;
        p.readable = false;
        p.invert = true;  // the stock firmware inverts this panel too
        p.rgb_order = false;
        p.bus_shared = false;
        panel.config(p);
        setPanel(&panel);
    }
};

Panel lcd;
const int BUF_LINES = 40;
const uint8_t TOUCH_ADDR = 0x15;
bool touch_ok, touched;

void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    const int32_t w = lv_area_get_width(a), h = lv_area_get_height(a);
    lcd.startWrite();
    lcd.setAddrWindow(a->x1, a->y1, w, h);
    lcd.writePixels((const uint16_t *)px, w * h, true);
    lcd.endWrite();
    lv_display_flush_ready(d);
}

bool touch_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

void touch_read(lv_indev_t *, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    touched = false;
    if (!touch_ok) return;
    // 0x01: gesture, finger count, X high/low, Y high/low.
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x01);
    if (Wire.endTransmission(false) != 0) return;
    uint8_t r[6];
    if (Wire.requestFrom(TOUCH_ADDR, (uint8_t)6) != 6) return;
    for (uint8_t &b : r) b = Wire.read();
    if ((r[1] & 0x0F) == 0) return;
    data->point.x = ((r[2] & 0x0F) << 8) | r[3];
    data->point.y = ((r[4] & 0x0F) << 8) | r[5];
    data->state = LV_INDEV_STATE_PRESSED;
    touched = true;
    ui_touch_activity();
}

uint32_t tick() { return millis(); }

}  // namespace

void display_begin() {
    ledcSetup(BL_CHANNEL, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, BL_CHANNEL);
    ledcWrite(BL_CHANNEL, 0);
    lcd.init();
    lcd.fillScreen(TFT_BLACK);

    // Touch controller: reset, keep it awake so polling always answers. The
    // I2C bus is shared with the sensors.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(10);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(60);
    touch_ok = touch_write(0xFE, 0x01);
    logf("display: touch %s", touch_ok ? "ok" : "not found");

    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *d = lv_display_create(240, 240);
    const size_t bytes = 240 * BUF_LINES * 2;
    void *b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_display_set_buffers(d, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, touch_read);
}

void display_set_backlight(uint8_t percent) {
    static int last = -1;
    if (percent > 100) percent = 100;
    if (percent == last) return;
    last = percent;
    ledcWrite(BL_CHANNEL, percent * 255 / 100);
}

bool display_touched() { return touched; }
