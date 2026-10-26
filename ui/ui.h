// KNOMI-NG user interface: swipeable screens built with LVGL.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "state.h"

enum class Gaze : uint8_t {
    Motion,  // eyes react to the KNOMI's own acceleration (toolhead mount)
    Nozzle,  // eyes follow the nozzle position (frame mount)
    Free,    // eyes just wander
};

struct UiOptions {
    uint32_t accent = 0xC6FF1A;  // main data colour (theme)
    uint32_t second = 0x00D1FF;  // secondary data colour (theme)
    uint32_t eyes = 0xF2F5F7;
    Gaze gaze = Gaze::Motion;
    bool invert_x = false;
    bool invert_y = false;
    bool clock24 = true;
    bool lanes_screen = true;    // Box Turtle screen when AFC is detected
    uint16_t sleep_after_s = 600; // idle time before the face falls asleep
    char hostname[32] = "knomi";
    char version[16] = "";
    // Runs a G-code script on the printer (touch commands). May be null.
    void (*gcode)(const char *script) = nullptr;
};

void ui_init(PrinterState *state, const UiOptions &opts);
void ui_set_options(const UiOptions &opts);
// Call every frame with the current time; updates screens from the state.
void ui_tick(uint32_t now_ms);
void ui_event(UiEvent ev);
// Any touch: keeps the current screen up and wakes the face.
void ui_touch_activity();
// PNG bytes of the current job's thumbnail; the UI keeps the pointer until the
// next call. Pass nullptr to clear.
void ui_set_thumbnail(const uint8_t *png, size_t len);
// True while the face is asleep: the platform dims the backlight.
bool ui_asleep();
// Jump to a screen (0 face, 1 print, 2 temps, 3 Box Turtle) as if swiped there.
void ui_show_tile(int index);
