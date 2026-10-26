#include "config.h"
#include <EEPROM.h>
#include <Preferences.h>
#include "log.h"

Config cfg;

namespace {

// Layout of the stock BTT firmware's knomi_config_t (stored with Arduino
// EEPROM, i.e. an NVS blob, after a 4-byte signature). Same member types, so
// the compiler gives the same padding.
struct StockConfig {
    char sta_ssid[33];
    char sta_pwd[65];
    int32_t sta_auth;
    char ap_ssid[33];
    char ap_pwd[65];
    char hostname[16];
    char moonraker_ip[65];
    char moonraker_port[6];
    char moonraker_tool[7];
    char mode[6];
    uint16_t theme_color;
};
const uint32_t STOCK_SIGN = 0x20231212;

void defaults() {
    memset(&cfg, 0, sizeof cfg);
    strcpy(cfg.hostname, "knomi");
    cfg.mr_port = 7125;
    strcpy(cfg.theme, "neon");
    cfg.eyes = 0xF2F5F7;
    cfg.gaze = 0;
    strcpy(cfg.tz, "UTC0");
    cfg.clock24 = true;
    cfg.afc_screen = true;
    cfg.sleep_s = 600;
    cfg.brightness = 100;
    cfg.sleep_brightness = 8;
}

void import_stock() {
    if (!EEPROM.begin(1024)) return;
    uint32_t sign = 0;
    EEPROM.get(0, sign);
    if (sign != STOCK_SIGN) {
        logf("config: no stock settings to import");
        EEPROM.end();
        return;
    }
    StockConfig st;
    EEPROM.get(4, st);
    EEPROM.end();
    st.sta_ssid[32] = st.sta_pwd[64] = st.hostname[15] = st.moonraker_ip[64] = 0;
    st.moonraker_port[5] = 0;
    strlcpy(cfg.wifi_ssid, st.sta_ssid, sizeof cfg.wifi_ssid);
    strlcpy(cfg.wifi_pass, st.sta_pwd, sizeof cfg.wifi_pass);
    if (st.hostname[0]) strlcpy(cfg.hostname, st.hostname, sizeof cfg.hostname);
    strlcpy(cfg.mr_host, st.moonraker_ip, sizeof cfg.mr_host);
    int port = atoi(st.moonraker_port);
    if (port > 0 && port < 65536) cfg.mr_port = port;
    logf("config: imported stock settings (Wi-Fi '%s', Moonraker %s:%u)", cfg.wifi_ssid,
         cfg.mr_host, cfg.mr_port);
}

}  // namespace

#define STR_FIELDS(X) X(wifi_ssid) X(wifi_pass) X(hostname) X(mr_host) X(mr_key) X(admin_pass) X(theme) X(tz)

void config_load() {
    defaults();
    Preferences p;
    p.begin("knomi-ng", true);
    const bool have = p.isKey("hostname");
#define GET(f) p.getString(#f, cfg.f, sizeof cfg.f);
    if (have) {
        STR_FIELDS(GET)
        cfg.mr_port = p.getUShort("mr_port", cfg.mr_port);
        cfg.eyes = p.getULong("eyes", cfg.eyes);
        cfg.gaze = p.getUChar("gaze", cfg.gaze);
        cfg.clock24 = p.getBool("clock24", cfg.clock24);
        cfg.afc_screen = p.getBool("afc_screen", cfg.afc_screen);
        cfg.track_invert_x = p.getBool("inv_x", false);
        cfg.track_invert_y = p.getBool("inv_y", false);
        cfg.sleep_s = p.getUShort("sleep_s", cfg.sleep_s);
        cfg.brightness = p.getUChar("bright", cfg.brightness);
        cfg.sleep_brightness = p.getUChar("sleep_bright", cfg.sleep_brightness);
    }
#undef GET
    p.end();
    if (have) return;
    import_stock();
    config_save();
}

void config_save() {
    Preferences p;
    p.begin("knomi-ng", false);
#define PUT(f) p.putString(#f, cfg.f);
    STR_FIELDS(PUT)
#undef PUT
    p.putUShort("mr_port", cfg.mr_port);
    p.putULong("eyes", cfg.eyes);
    p.putUChar("gaze", cfg.gaze);
    p.putBool("clock24", cfg.clock24);
    p.putBool("afc_screen", cfg.afc_screen);
    p.putBool("inv_x", cfg.track_invert_x);
    p.putBool("inv_y", cfg.track_invert_y);
    p.putUShort("sleep_s", cfg.sleep_s);
    p.putUChar("bright", cfg.brightness);
    p.putUChar("sleep_bright", cfg.sleep_brightness);
    p.end();
}
