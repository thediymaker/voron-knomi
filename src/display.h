// GC9A01 round LCD + CST816S touch + PWM backlight, wired into LVGL.
#pragma once
#include <stdint.h>

void display_begin();
// Backlight in percent (0 = off).
void display_set_backlight(uint8_t percent);
// True while a finger is on the screen.
bool display_touched();
