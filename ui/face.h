// Procedurally drawn robot eyes.
#pragma once
#include <math.h>
#include "lvgl.h"

enum class Mood : uint8_t {
    Neutral, Happy, Focused, Sleepy, Asleep, Surprised,
    Angry, Sad, Squint, Confused, Worried,
};

lv_obj_t *face_create(lv_obj_t *parent);
void face_set_mood(Mood m);
Mood face_mood();
void face_set_color(lv_color_t c);  // eye colour (angry/worried override it)
// Gaze in -1..1 (x right, y down); NAN lets the eyes wander on their own.
void face_look(float x, float y);
void face_set_question(bool on);
// Night: smaller eyes lower down, leaving room for the clock above.
void face_set_night(bool on);
void face_tick(uint32_t now_ms);
