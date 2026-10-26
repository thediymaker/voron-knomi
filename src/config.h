// Persistent settings (NVS namespace "knomi-ng").
#pragma once
#include <stdint.h>

struct Config {
    char wifi_ssid[33];
    char wifi_pass[65];
    char hostname[32];
    char mr_host[65];
    uint16_t mr_port;
    char mr_key[65];       // Moonraker API key, optional
    char admin_pass[65];   // web UI password; empty until first setup
    char theme[12];        // ThemePreset id
    uint32_t eyes;         // eye colour; EYES_ACCENT = follow the theme
    uint8_t gaze;          // Gaze: 0 motion, 1 nozzle, 2 free
    char tz[48];           // POSIX TZ string
    bool clock24;
    bool afc_screen;       // Box Turtle screen when AFC is detected
    bool track_invert_x, track_invert_y;
    uint16_t sleep_s;
    uint8_t brightness;    // percent
    uint8_t sleep_brightness;
};

static const uint32_t EYES_ACCENT = 0x1000000;

extern Config cfg;

// Loads settings. On the first boot after the stock firmware, Wi-Fi and
// Moonraker settings are copied from the stock firmware's EEPROM area.
void config_load();
void config_save();
