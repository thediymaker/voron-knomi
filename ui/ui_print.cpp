// Print screen: thumbnail inside the progress ring. During print start it
// shows the macro's stage and the heat-up rings; a finished print gets
// confetti and stays up until someone taps the screen.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

namespace ui {
namespace {

lv_obj_t *tile, *p_arc, *p_arc2, *p_img, *p_pct, *p_time, *p_layer, *p_name, *p_idle, *p_stage;
lv_obj_t *confetti;
lv_image_dsc_t thumb;
bool have_thumb;

bool done_pending;      // finished print not acknowledged
uint32_t celebrate_ms;  // start of the confetti
uint32_t confetti_last; // time of the previous step, relative to celebrate_ms
const uint32_t CONFETTI_MS = 5500;

struct Bit { float x, y, vx, vy, phase, spin; uint32_t c; uint8_t size; };
Bit bits[96];

void burst(int from, int to) {
    const uint32_t cols[] = {O.accent, O.second, 0xFFFFFF, 0xFF453A, 0xFFD60A, 0xBF5AF2};
    for (int i = from; i < to; i++) {
        Bit &b = bits[i];
        const float a = (-90 + (rand() % 140 - 70)) * (float)M_PI / 180;
        const float v = 3.0f + (rand() % 100) / 25.0f;
        b.x = 120 + (rand() % 30 - 15);
        b.y = 132;
        b.vx = cosf(a) * v;
        b.vy = sinf(a) * v;
        b.phase = (rand() % 628) / 100.0f;
        b.spin = 0.15f + (rand() % 30) / 100.0f;
        b.c = cols[rand() % 6];
        b.size = 4 + rand() % 4;
    }
}

void confetti_draw(lv_event_t *e) {
    if (!celebrate_ms) return;
    const uint32_t t = now_ms - celebrate_ms;
    if (t > CONFETTI_MS) return;
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(confetti, &a);
    const lv_opa_t opa = t < CONFETTI_MS - 800 ? LV_OPA_COVER : (lv_opa_t)(255 * (CONFETTI_MS - t) / 800);
    for (const Bit &b : bits) {
        // Tumbling: the width follows the spin.
        const float w = b.size * (0.25f + 0.75f * fabsf(cosf(b.phase)));
        draw_rect(l, a.x1 + b.x - w, a.y1 + b.y - b.size * 0.45f, a.x1 + b.x + w,
                  a.y1 + b.y + b.size * 0.45f, 1, b.c, opa);
    }
}

void confetti_step() {
    if (!celebrate_ms) return;
    const uint32_t t = now_ms - celebrate_ms;
    if (t > CONFETTI_MS) {
        show(confetti, false);
        return;
    }
    // Two more bursts after the first one.
    if (confetti_last < 1100 && t >= 1100) burst(32, 64);
    if (confetti_last < 2200 && t >= 2200) burst(64, 96);
    confetti_last = t;
    for (Bit &b : bits) {
        b.vy += 0.16f;
        b.vx *= 0.985f;
        b.x += b.vx;
        b.y += b.vy;
        b.phase += b.spin;
    }
    lv_obj_invalidate(confetti);
}

void on_tap(lv_event_t *) {
    if (done_pending) done_pending = false;
}

}  // namespace

void print_build(lv_obj_t *t) {
    tile = t;
    lv_obj_add_event_cb(t, on_tap, LV_EVENT_SHORT_CLICKED, nullptr);
    p_arc = ring(t, 236, 8, O.accent, 270, 360, 1000);
    p_arc2 = ring(t, 212, 6, O.accent, 270, 360, 1000);
    p_img = lv_image_create(t);
    lv_obj_align(p_img, LV_ALIGN_CENTER, 0, -4);
    lv_obj_remove_flag(p_img, LV_OBJ_FLAG_CLICKABLE);
    show(p_img, false);
    p_time = label(t, &lv_font_montserrat_20, COL_TEXT, LV_ALIGN_TOP_MID, 0, 22);
    p_name = label(t, &lv_font_montserrat_12, COL_MUTED, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_width(p_name, 140);
    lv_label_set_long_mode(p_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
    p_pct = label(t, &lv_font_montserrat_28, COL_TEXT, LV_ALIGN_BOTTOM_MID, 0, -42);
    p_layer = label(t, &lv_font_montserrat_12, COL_MUTED, LV_ALIGN_BOTTOM_MID, 0, -26);
    p_stage = label(t, &lv_font_montserrat_20, COL_TEXT, LV_ALIGN_CENTER, 0, -4);
    lv_obj_set_width(p_stage, 160);
    lv_label_set_long_mode(p_stage, LV_LABEL_LONG_WRAP);
    p_idle = label(t, &lv_font_montserrat_20, COL_MUTED, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(p_idle, "Nothing printing");

    confetti = lv_obj_create(t);
    lv_obj_remove_style_all(confetti);
    lv_obj_set_size(confetti, 240, 240);
    lv_obj_remove_flag(confetti, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(confetti, confetti_draw, LV_EVENT_DRAW_MAIN, nullptr);
    show(confetti, false);
}

void print_apply_theme() {
    if (!p_arc) return;
    lv_obj_set_style_arc_color(p_arc, lv_color_hex(O.accent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(p_arc2, lv_color_hex(O.accent), LV_PART_INDICATOR);
}

void print_celebrate() {
    done_pending = true;
    celebrate_ms = now_ms ? now_ms : 1;
    confetti_last = 0;
    burst(0, 32);
    for (int i = 32; i < 96; i++) bits[i].y = 999;  // later bursts
    show(confetti, true);
}

bool print_wants_focus() {
    if (done_pending && S->job != Job::Complete) done_pending = false;  // new job / cleared
    return done_pending;
}

void print_update() {
    confetti_step();
    const bool has_job = online() && S->filename[0] &&
                         (job_active() || S->job == Job::Complete || S->job == Job::Cancelled);
    const bool prep = has_job && job_preparing();
    show(p_idle, !has_job);
    show(p_arc, has_job);
    show(p_arc2, prep);
    show(p_time, has_job);
    show(p_name, has_job && !prep);
    show(p_pct, has_job && !prep);
    show(p_layer, has_job);
    show(p_stage, prep);
    show(p_img, has_job && have_thumb);
    if (!has_job) return;
    lv_obj_set_style_image_opa(p_img, prep ? LV_OPA_30 : LV_OPA_COVER, 0);

    char buf[96], dur[24];
    if (prep) {
        // Outer ring: bed heat, inner ring: nozzle heat.
        const float bp = S->bed_target > 0 ? S->bed / S->bed_target : 0;
        const float np = S->nozzle_target > 0 ? S->nozzle / S->nozzle_target : 0;
        lv_obj_set_style_arc_color(p_arc, lv_color_hex(O.second), LV_PART_INDICATOR);
        lv_arc_set_value(p_arc, (int32_t)(fminf(1, fmaxf(0, bp)) * 1000));
        lv_arc_set_value(p_arc2, (int32_t)(fminf(1, fmaxf(0, np)) * 1000));
        set_text(p_time, "Getting ready");
        set_text(p_stage, S->display_msg[0] ? S->display_msg : "Starting");
        if (S->nozzle_target > 0)
            snprintf(buf, sizeof buf, "bed %d\xC2\xB0  nozzle %d\xC2\xB0", (int)S->bed, (int)S->nozzle);
        else
            snprintf(buf, sizeof buf, "bed %d / %d\xC2\xB0", (int)S->bed, (int)S->bed_target);
        set_text(p_layer, buf);
        return;
    }

    lv_obj_set_style_arc_color(p_arc, lv_color_hex(O.accent), LV_PART_INDICATOR);
    lv_arc_set_value(p_arc, (int32_t)(S->progress * 1000));
    snprintf(buf, sizeof buf, "%d%%", (int)(S->progress * 100));

    if (S->job == Job::Complete) {
        fmt_duration(dur, sizeof dur, S->print_duration);
        set_text(p_time, "Done!");
        snprintf(buf, sizeof buf, "%s", dur);
        set_text(p_pct, buf);
        if (S->filament_used > 0) snprintf(buf, sizeof buf, "%.1f m of filament", S->filament_used / 1000);
        else buf[0] = 0;
        set_text(p_layer, buf);
    } else {
        set_text(p_pct, buf);
        if (S->job == Job::Cancelled) {
            snprintf(buf, sizeof buf, "Cancelled");
        } else if (S->job == Job::Paused) {
            snprintf(buf, sizeof buf, "Paused");
        } else {
            float left = -1;
            if (S->slicer_estimate > 0) left = fmaxf(0, S->slicer_estimate - S->print_duration);
            else if (S->progress > 0.02f) left = S->print_duration / S->progress - S->print_duration;
            if (left >= 0) {
                fmt_duration(dur, sizeof dur, left);
                snprintf(buf, sizeof buf, "%s left", dur);
            } else {
                snprintf(buf, sizeof buf, "Printing");
            }
        }
        set_text(p_time, buf);
        if (S->layers > 0) snprintf(buf, sizeof buf, "Layer %d / %d", S->layer, S->layers);
        else buf[0] = 0;
        set_text(p_layer, buf);
    }

    // File name without directories or the extension.
    const char *base = strrchr(S->filename, '/');
    base = base ? base + 1 : S->filename;
    snprintf(buf, sizeof buf, "%s", base);
    char *dot = strrchr(buf, '.');
    if (dot) *dot = 0;
    set_text(p_name, buf);
}

void print_set_thumbnail(const uint8_t *png, size_t len) {
    if (have_thumb) lv_image_cache_drop(&thumb);
    have_thumb = false;
    if (!png || len < 24 || png[0] != 0x89 || png[1] != 'P') {
        lv_image_set_src(p_img, nullptr);
        return;
    }
    const uint32_t w = (uint32_t)png[16] << 24 | png[17] << 16 | png[18] << 8 | png[19];
    const uint32_t h = (uint32_t)png[20] << 24 | png[21] << 16 | png[22] << 8 | png[23];
    memset(&thumb, 0, sizeof thumb);
    thumb.header.magic = LV_IMAGE_HEADER_MAGIC;
    thumb.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    thumb.header.w = w;
    thumb.header.h = h;
    thumb.data = png;
    thumb.data_size = len;
    lv_image_set_src(p_img, &thumb);
    const uint32_t big = w > h ? w : h;
    lv_image_set_scale(p_img, big ? 124 * 256 / big : 256);
    have_thumb = true;
}

}  // namespace ui
