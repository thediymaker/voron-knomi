// Printer state shared between the Moonraker client and the UI.
// Plain C++ so the UI also builds in the desktop simulator.
#pragma once
#include <stdint.h>

enum class Klippy : uint8_t { Offline, Startup, Ready, Shutdown, Error };
enum class Job : uint8_t { Standby, Printing, Paused, Complete, Cancelled, Error };

struct Lane {
    char name[16];      // "lane1"
    char map[8];        // "T0"
    char material[16];  // "PLA"
    char status[24];    // AFC status: "Loaded", "Tooled", "None", ...
    uint32_t color;     // 0xRRGGBB
    bool has_color;
    bool prep;          // filament at the prep sensor
    bool load;          // filament at the load sensor
    bool tool_loaded;   // this lane is in the toolhead
    float weight;       // grams left, < 0 when unknown
    float initial_weight; // grams on a full spool, <= 0 when unknown
};

static const int MAX_LANES = 8;

struct PrinterState {
    // network
    bool wifi = false;
    bool ap_mode = false;
    char ip[16] = "";
    char ap_ssid[33] = "";
    char ap_pass[16] = "";
    bool moonraker = false;  // websocket connected

    // klippy
    Klippy klippy = Klippy::Offline;
    char klippy_msg[96] = "";

    // job
    Job job = Job::Standby;
    char filename[128] = "";
    float progress = 0;        // 0..1
    float print_duration = 0;  // s
    float slicer_estimate = 0; // s, 0 when unknown
    int layer = -1, layers = -1;
    float filament_used = 0;   // mm
    bool busy = false;         // idle_timeout says "Printing" (any gcode running)
    char display_msg[128] = "";  // SET_DISPLAY_TEXT / M117
    uint32_t display_msg_ms = 0; // when it last changed

    // heaters
    float nozzle = 0, nozzle_target = 0;
    float bed = 0, bed_target = 0;

    // motion
    float pos[3] = {0, 0, 0};
    float axis_max[2] = {350, 350};
    float velocity = 0;
    char homed[4] = "";

    // AFC (Box Turtle)
    bool afc = false;
    int lane_count = 0;
    Lane lanes[MAX_LANES] = {};
    char afc_lane[16] = "";      // lane in the toolhead ("current_load")
    char afc_moving[16] = "";    // lane being loaded/unloaded ("current_lane")
    char afc_state[24] = "";     // "Idle", "Loading", "Unloading", ...
    bool afc_error = false;
    char afc_msg[160] = "";
    int toolchange = 0, toolchanges = 0;
    bool afc_led = false;

    // KNOMI's own sensors
    bool has_air = false;
    float air_temp = 0, air_hum = 0;
    bool has_motion = false;
    float motion[2] = {0, 0};  // smoothed acceleration without gravity, g (x, y)

    // local time (from NTP)
    bool time_ok = false;
    int hour = 0, minute = 0;
    char date[24] = "";

    // timestamps (ms) of console events the face reacts to
    uint32_t probe_ms = 0;
    uint32_t gcode_error_ms = 0;
};

enum class UiEvent : uint8_t {
    Poke,        // screen tapped
    Bump,        // KNOMI knocked (accelerometer)
    JobDone,
    JobCancelled,
};

int lane_index(const PrinterState &s, const char *name);
