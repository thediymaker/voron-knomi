// Shared between the UI's screen files.
#pragma once
#include "theme.h"
#include "ui.h"

namespace ui {

extern PrinterState *S;
extern UiOptions O;
extern uint32_t now_ms;

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_align_t align,
                int32_t x, int32_t y);
lv_obj_t *ring(lv_obj_t *parent, int32_t size, int32_t width, uint32_t color, int32_t rot,
               int32_t sweep, int32_t max);
void set_text(lv_obj_t *l, const char *txt);
void show(lv_obj_t *o, bool on);
void fmt_duration(char *out, size_t n, float s);
bool online();
bool job_active();
bool heating();
bool afc_busy();
void send_gcode(const char *script);
// Print start: the job is running but nothing has been extruded yet.
bool job_preparing();

// Draw helpers for custom-drawn objects.
void draw_rect(lv_layer_t *l, float x1, float y1, float x2, float y2, int32_t r, uint32_t c,
               lv_opa_t opa = LV_OPA_COVER);
void draw_circle(lv_layer_t *l, float cx, float cy, float r, uint32_t c, lv_opa_t opa = LV_OPA_COVER);
void draw_tri(lv_layer_t *l, float x0, float y0, float x1, float y1, float x2, float y2,
              uint32_t c, lv_opa_t opa = LV_OPA_COVER);
void draw_arc(lv_layer_t *l, float cx, float cy, float r, int32_t w, float a0, float a1,
              uint32_t c, lv_opa_t opa = LV_OPA_COVER, bool rounded = true);
void draw_hex(lv_layer_t *l, float cx, float cy, float r, uint32_t c, lv_opa_t opa = LV_OPA_COVER);

// Print screen (ui_print.cpp)
void print_build(lv_obj_t *tile);
void print_update();
void print_apply_theme();
void print_set_thumbnail(const uint8_t *png, size_t len);
void print_celebrate();
bool print_wants_focus();  // finished job not acknowledged yet

// Box Turtle screen (ui_lanes.cpp)
void lanes_build(lv_obj_t *tile);
void lanes_update();
bool lanes_wants_focus();  // a lane or menu is selected

}  // namespace ui
