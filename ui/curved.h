// Text laid along the round screen's edge: one rotated label per character
// (LVGL 9.2 has no arc label).
#pragma once
#include "lvgl.h"

// A full-screen container (240x240, centre at 120,120) holding up to
// max_chars letters.
lv_obj_t *curved_create(lv_obj_t *parent, int max_chars);
// Lays txt along a circle of `radius` around the screen centre, centred on
// the bottom (bottom=true, reads left to right with letters upright) or the
// top of the screen.
void curved_set_text(lv_obj_t *c, const char *txt, const lv_font_t *font, uint32_t color,
                     float radius, bool bottom);
