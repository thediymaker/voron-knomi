// KNOMI-NG: alternative firmware for the BTT KNOMI V2.
#include <Arduino.h>
#include <esp_ota_ops.h>
#include "config.h"
#include "display.h"
#include "log.h"
#include "moonraker.h"
#include "net.h"
#include "sensors.h"
#include "theme.h"
#include "ui.h"
#include "version.h"
#include "web.h"

static PrinterState state;
static uint8_t *thumb;

// Crash-loop guard: three crash resets in a row (before a minute of uptime)
// boot whatever firmware is in the other OTA slot, i.e. the stock firmware
// after the first install. RTC memory survives resets but not power-up.
RTC_NOINIT_ATTR static uint32_t guard_magic, guard_crashes;
static bool guard_cleared;

static void boot_guard() {
    const esp_reset_reason_t r = esp_reset_reason();
    if (guard_magic != 0x4B4E4F4D || r == ESP_RST_POWERON) {
        guard_magic = 0x4B4E4F4D;
        guard_crashes = 0;
    }
    const bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
                       r == ESP_RST_WDT;
    guard_crashes = crash ? guard_crashes + 1 : 0;
    logf("boot: KNOMI-NG %s, reset reason %d, crashes in a row %u", FW_VERSION, (int)r,
         guard_crashes);
    if (guard_crashes >= 3) {
        guard_crashes = 0;
        const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
        esp_app_desc_t d;
        if (other && esp_ota_get_partition_description(other, &d) == ESP_OK) {
            logf("boot: crash loop, falling back to %s %s", d.project_name, d.version);
            esp_ota_set_boot_partition(other);
            esp_restart();
        }
    }
}

void apply_ui_options() {
    UiOptions o;
    const ThemePreset &t = theme_find(cfg.theme);
    o.accent = t.accent;
    o.second = t.second;
    o.eyes = cfg.eyes == EYES_ACCENT ? t.accent : cfg.eyes;
    o.gaze = cfg.gaze == 1 ? Gaze::Nozzle : cfg.gaze == 2 ? Gaze::Free : Gaze::Motion;
    o.invert_x = cfg.track_invert_x;
    o.invert_y = cfg.track_invert_y;
    o.clock24 = cfg.clock24;
    o.lanes_screen = cfg.afc_screen;
    o.sleep_after_s = cfg.sleep_s;
    o.gcode = mr_gcode;
    strlcpy(o.hostname, cfg.hostname, sizeof o.hostname);
    strlcpy(o.version, FW_VERSION, sizeof o.version);
    ui_set_options(o);
}

void setup() {
    Serial.begin(115200);
    boot_guard();
    config_load();
    display_begin();
    UiOptions o;
    ui_init(&state, o);
    apply_ui_options();
    lv_timer_handler();
    display_set_backlight(cfg.brightness);
    sensors_begin();
    net_begin(state);
    web_begin(state);
    mr_begin(state);
    esp_ota_mark_app_valid_cancel_rollback();
}

void apply_timezone() { configTzTime(cfg.tz, "pool.ntp.org", "time.google.com"); }

static void update_clock(uint32_t now) {
    static uint32_t next;
    static bool ntp_started;
    if (now < next) return;
    next = now + 1000;
    if (!ntp_started && state.wifi) {
        apply_timezone();
        ntp_started = true;
    }
    time_t t = time(nullptr);
    if (t < 1700000000) return;  // not synced yet
    struct tm lt;
    localtime_r(&t, &lt);
    state.time_ok = true;
    state.hour = lt.tm_hour;
    state.minute = lt.tm_min;
    strftime(state.date, sizeof state.date, "%A %d %B", &lt);
}

void loop() {
    const uint32_t now = millis();
    net_loop(now, state);
    web_loop();
    mr_loop(now);
    if (sensors_loop(now, state)) ui_event(UiEvent::Bump);
    update_clock(now);

    uint8_t *png;
    size_t len;
    if (mr_take_thumbnail(&png, &len)) {
        ui_set_thumbnail(png, len);
        free(thumb);
        thumb = png;
    }

    static uint32_t last_ui;
    if (now - last_ui >= 33) {
        last_ui = now;
        ui_tick(now);
        display_set_backlight(ui_asleep() && !display_touched() ? cfg.sleep_brightness
                                                                : cfg.brightness);
    }
    lv_timer_handler();

    if (!guard_cleared && now > 60000) {
        guard_crashes = 0;
        guard_cleared = true;
    }
    delay(1);
}
