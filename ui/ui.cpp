// Screens: face | print | temperatures | Box Turtle (only when an AFC unit
// is detected and the screen is enabled). Swipe between them;
// after 30 s without a touch the UI goes back to the face, or to whatever
// needs attention (AFC busy or in error, a finished print).
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "curved.h"
#include "face.h"
#include "ui_internal.h"

int lane_index(const PrinterState &s, const char *name) {
    for (int i = 0; i < s.lane_count; i++)
        if (strcmp(s.lanes[i].name, name) == 0) return i;
    return -1;
}

namespace ui {

PrinterState *S;
UiOptions O;
uint32_t now_ms;

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_align_t align,
                int32_t x, int32_t y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, align, x, y);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *ring(lv_obj_t *parent, int32_t size, int32_t width, uint32_t color, int32_t rot,
               int32_t sweep, int32_t max) {
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_center(a);
    lv_arc_set_rotation(a, rot);
    lv_arc_set_bg_angles(a, 0, sweep);
    lv_arc_set_range(a, 0, max);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
    return a;
}

void set_text(lv_obj_t *l, const char *txt) {
    // Skip identical text: lv_label_set_text always invalidates.
    if (strcmp(lv_label_get_text(l), txt) != 0) lv_label_set_text(l, txt);
}

void show(lv_obj_t *o, bool on) {
    if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

void fmt_duration(char *out, size_t n, float s) {
    int m = (int)(s / 60 + 0.5f);
    if (m >= 60) snprintf(out, n, "%dh %02dm", m / 60, m % 60);
    else if (m >= 1) snprintf(out, n, "%dm", m);
    else snprintf(out, n, "%ds", (int)s);
}

bool online() { return S->wifi && S->moonraker && S->klippy == Klippy::Ready; }

bool job_active() { return S->job == Job::Printing || S->job == Job::Paused; }

bool job_preparing() { return S->job == Job::Printing && S->progress < 0.0005f; }

bool heating() {
    return (S->nozzle_target > 0 && S->nozzle < S->nozzle_target - 4) ||
           (S->bed_target > 0 && S->bed < S->bed_target - 2);
}

bool afc_busy() {
    return S->afc_error ||
           (S->afc_state[0] && strcmp(S->afc_state, "Idle") != 0 && S->afc_moving[0]);
}

void send_gcode(const char *script) {
    if (O.gcode) O.gcode(script);
}

void draw_rect(lv_layer_t *l, float x1, float y1, float x2, float y2, int32_t r, uint32_t c,
               lv_opa_t opa) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(c);
    d.bg_opa = opa;
    d.radius = r;
    lv_area_t a = {(int32_t)lroundf(x1), (int32_t)lroundf(y1), (int32_t)lroundf(x2),
                   (int32_t)lroundf(y2)};
    lv_draw_rect(l, &d, &a);
}

void draw_circle(lv_layer_t *l, float cx, float cy, float r, uint32_t c, lv_opa_t opa) {
    draw_rect(l, cx - r, cy - r, cx + r, cy + r, LV_RADIUS_CIRCLE, c, opa);
}

void draw_tri(lv_layer_t *l, float x0, float y0, float x1, float y1, float x2, float y2,
              uint32_t c, lv_opa_t opa) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.bg_color = lv_color_hex(c);
    d.bg_opa = opa;
    d.p[0] = {(lv_value_precise_t)x0, (lv_value_precise_t)y0};
    d.p[1] = {(lv_value_precise_t)x1, (lv_value_precise_t)y1};
    d.p[2] = {(lv_value_precise_t)x2, (lv_value_precise_t)y2};
    lv_draw_triangle(l, &d);
}

void draw_arc(lv_layer_t *l, float cx, float cy, float r, int32_t w, float a0, float a1,
              uint32_t c, lv_opa_t opa, bool rounded) {
    if (a1 - a0 < 0.5f) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center = {(int32_t)lroundf(cx), (int32_t)lroundf(cy)};
    d.radius = (uint16_t)r;
    d.width = w;
    d.start_angle = a0;
    d.end_angle = a1;
    d.color = lv_color_hex(c);
    d.opa = opa;
    d.rounded = rounded;
    lv_draw_arc(l, &d);
}

// Pointy-top hexagon: a rectangle with a triangle on top and bottom, so no
// two shapes share a diagonal edge (that leaves an anti-aliased seam).
void draw_hex(lv_layer_t *l, float cx, float cy, float r, uint32_t c, lv_opa_t opa) {
    const float hw = r * 0.866f;
    const float x1 = floorf(cx - hw), x2 = ceilf(cx + hw);
    const float y1 = floorf(cy - r / 2), y2 = ceilf(cy + r / 2);
    draw_rect(l, x1, y1, x2, y2, 0, c, opa);
    draw_tri(l, x1, y1 + 1, x2, y1 + 1, cx, cy - r, c, opa);
    draw_tri(l, x1, y2 - 1, x2, y2 - 1, cx, cy + r, c, opa);
}

}  // namespace ui

using namespace ui;

namespace {

enum Tile { T_FACE, T_PRINT, T_TEMPS, T_LANES, T_COUNT };
const uint32_t MANUAL_TIMEOUT_MS = 30000;

lv_obj_t *tv, *tiles[T_COUNT];
lv_obj_t *face, *rim, *face_text, *clock_time, *clock_date;
lv_obj_t *info, *info_lbl;
lv_obj_t *t_noz_arc, *t_bed_arc, *t_noz, *t_noz_tgt, *t_bed, *t_air;

uint32_t touch_ms, activity_ms, surprise_until, happy_until, sad_until, info_until;
uint32_t afc_busy_until;
bool manual;
Job last_job = Job::Standby;
float last_pos[3], last_targets;
// Motion gaze: the eyes dart against the toolhead's acceleration (inertia).
// Printing accelerations flip direction several times a second, so the
// response is quick and strong rather than a slow spring.
float gaze_x, gaze_y;
uint32_t gaze_ms;

void gaze_step(float ax, float ay) {
    float dt = gaze_ms ? (now_ms - gaze_ms) / 1000.0f : 0;
    gaze_ms = now_ms;
    if (dt > 0.1f) dt = 0.1f;
    const float gain = 7;  // 0.14 g = full deflection
    const float k = 1 - expf(-dt / 0.09f);
    gaze_x += (fmaxf(-1, fminf(1, gain * ax)) - gaze_x) * k;
    gaze_y += (fmaxf(-1, fminf(1, gain * ay)) - gaze_y) * k;
}

// ---------------------------------------------------------------- face

void on_face_click(lv_event_t *) { ui_event(UiEvent::Poke); }
void on_face_long(lv_event_t *) { info_until = now_ms + 8000; }

void build_face(lv_obj_t *t) {
    clock_time = label(t, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_TOP_MID, 0, 52);
    clock_date = label(t, &lv_font_montserrat_14, COL_MUTED, LV_ALIGN_TOP_MID, 0, 108);
    face = face_create(t);
    lv_obj_add_event_cb(face, on_face_click, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(face, on_face_long, LV_EVENT_LONG_PRESSED, nullptr);
    rim = ring(t, 236, 5, O.accent, 270, 360, 1000);
    lv_obj_set_style_arc_opa(rim, LV_OPA_40, LV_PART_MAIN);
    show(rim, false);
    face_text = curved_create(t, 48);
}

// The printer's display message: print-start stages stay up while the
// printer gets ready; anything else (the random sayings) shows for 20 s when
// it appears and again for 15 s every 10 minutes.
bool show_display_msg() {
    if (job_preparing() || (S->busy && !job_active())) return true;
    if (!job_active()) return false;
    const uint32_t age = now_ms - S->display_msg_ms;
    return age < 20000 || age % 600000 < 15000;
}

// "Soak for 1 min" style display messages get a countdown.
int soak_seconds_left() {
    const char *p = strstr(S->display_msg, "oak for ");
    if (!p) return -1;
    const int n = atoi(p + 8);
    if (n <= 0) return -1;
    const int total = strstr(p, "sec") ? n : n * 60;
    const int left = total - (int)((now_ms - S->display_msg_ms) / 1000);
    return left > 0 ? left : 0;
}

void update_face() {
    const uint32_t sleep_ms = O.sleep_after_s * 1000u;
    const uint32_t idle = now_ms - activity_ms;
    char status[64] = "";
    uint32_t status_col = COL_MUTED;
    Mood m = Mood::Neutral;
    bool question = false;
    float gx = NAN, gy = NAN;

    if (!S->wifi) {
        m = Mood::Confused; question = true;
        snprintf(status, sizeof status, "%s", S->ap_mode ? "Wi-Fi setup" : "No Wi-Fi");
    } else if (!S->moonraker) {
        m = Mood::Confused; question = true;
        snprintf(status, sizeof status, "No printer");
    } else if (S->klippy == Klippy::Startup || S->klippy == Klippy::Offline) {
        m = Mood::Sleepy;
        snprintf(status, sizeof status, "Klipper starting");
    } else if (S->klippy != Klippy::Ready || S->job == Job::Error) {
        m = Mood::Angry;
        snprintf(status, sizeof status, "%s",
                 S->klippy == Klippy::Shutdown ? "Klipper shutdown" : "Klipper error");
        status_col = COL_RED;
    } else if (S->afc_error) {
        m = Mood::Worried;
        snprintf(status, sizeof status, "Box Turtle needs help");
        status_col = COL_AMBER;
    } else if (now_ms < surprise_until) {
        m = Mood::Surprised;
    } else if (now_ms < happy_until) {
        m = Mood::Happy;
    } else if (now_ms < sad_until) {
        m = Mood::Sad;
    } else if (S->gcode_error_ms && now_ms - S->gcode_error_ms < 6000) {
        m = Mood::Angry;
    } else if (S->probe_ms && now_ms - S->probe_ms < 2500) {
        m = Mood::Squint; gx = 0; gy = 0.85f;
    } else if (S->job == Job::Printing || heating()) {
        m = Mood::Focused;
    } else if (S->job == Job::Paused) {
        snprintf(status, sizeof status, "Paused");
    } else if (idle > sleep_ms) {
        m = Mood::Asleep;
    } else if (idle > sleep_ms / 2) {
        m = Mood::Sleepy;
    }

    if (isnan(gx)) {
        if (O.gaze == Gaze::Motion && S->has_motion) {
            // Inertia: the eyes swing against the toolhead's acceleration.
            const float sx = O.invert_x ? 1 : -1, sy = O.invert_y ? 1 : -1;
            gaze_step(sx * S->motion[0], sy * S->motion[1]);
            // While printing the eyes always ride the spring; otherwise they
            // wander once it has settled.
            const bool moving = fabsf(gaze_x) > 0.06f || fabsf(gaze_y) > 0.06f;
            if (moving || S->job == Job::Printing) { gx = gaze_x; gy = gaze_y; }
        } else if (O.gaze == Gaze::Nozzle && online() && strchr(S->homed, 'x') &&
                   strchr(S->homed, 'y') && (S->job == Job::Printing || S->busy)) {
            gx = S->pos[0] / S->axis_max[0] * 2 - 1;
            gy = -(S->pos[1] / S->axis_max[1] * 2 - 1) * 0.7f;  // back of the bed = look up
            if (O.invert_x) gx = -gx;
            if (O.invert_y) gy = -gy;
        }
    }

    face_set_mood(m);
    face_look(gx, gy);
    face_set_question(question);

    // Night clock while asleep.
    const bool night = m == Mood::Asleep && S->time_ok;
    face_set_night(night);
    show(clock_time, night);
    show(clock_date, night);
    if (night) {
        char t[8];
        int h = S->hour;
        if (!O.clock24) h = h % 12 ? h % 12 : 12;
        snprintf(t, sizeof t, O.clock24 ? "%02d:%02d" : "%d:%02d", h, S->minute);
        set_text(clock_time, t);
        set_text(clock_date, S->date);
    }

    // Bottom edge: status, else the printer's display message (print-start
    // stages, random sayings), with a soak countdown where it applies.
    char msg[160];
    uint32_t msg_col = status_col;
    const int soak = online() ? soak_seconds_left() : -1;
    if (status[0]) {
        snprintf(msg, sizeof msg, "%s", status);
    } else if (soak >= 0) {
        snprintf(msg, sizeof msg, "Soak  %d:%02d", soak / 60, soak % 60);
        msg_col = O.accent;
    } else if (online() && S->display_msg[0] && show_display_msg()) {
        snprintf(msg, sizeof msg, "%s", S->display_msg);
        msg_col = O.accent;
    } else {
        msg[0] = 0;
    }
    curved_set_text(face_text, msg,
                    strlen(msg) > 30 ? &lv_font_montserrat_12 : &lv_font_montserrat_14, msg_col,
                    104, true);

    // Rim: print progress; during print start, heat-up or soak progress.
    if (online() && job_active() && !job_preparing()) {
        lv_obj_set_style_arc_color(rim, lv_color_hex(O.accent), LV_PART_INDICATOR);
        lv_arc_set_value(rim, (int32_t)(S->progress * 1000));
        show(rim, true);
    } else if (online() && heating()) {
        const bool bed = S->bed_target > 0 && S->bed < S->bed_target - 2;
        const float p = bed ? S->bed / S->bed_target : S->nozzle / S->nozzle_target;
        lv_obj_set_style_arc_color(rim, lv_color_hex(bed ? O.second : O.accent), LV_PART_INDICATOR);
        lv_arc_set_value(rim, (int32_t)(fmaxf(0, fminf(1, p)) * 1000));
        show(rim, true);
    } else if (soak >= 0) {
        lv_obj_set_style_arc_color(rim, lv_color_hex(O.accent), LV_PART_INDICATOR);
        lv_arc_set_value(rim, 1000 - soak * 1000 / 60);
        show(rim, true);
    } else {
        show(rim, false);
    }
}

// ---------------------------------------------------------------- temps

void build_temps(lv_obj_t *t) {
    t_noz_arc = ring(t, 226, 12, O.accent, 135, 270, 300);
    t_bed_arc = ring(t, 192, 10, O.second, 135, 270, 120);
    t_noz = label(t, &lv_font_montserrat_48, COL_TEXT, LV_ALIGN_CENTER, 0, -22);
    t_noz_tgt = label(t, &lv_font_montserrat_14, O.accent, LV_ALIGN_CENTER, 0, 14);
    t_bed = label(t, &lv_font_montserrat_20, COL_TEXT, LV_ALIGN_CENTER, 0, 44);
    t_air = label(t, &lv_font_montserrat_12, COL_MUTED, LV_ALIGN_BOTTOM_MID, 0, -30);
}

void update_temps() {
    char buf[48];
    lv_arc_set_value(t_noz_arc, (int32_t)S->nozzle);
    lv_arc_set_value(t_bed_arc, (int32_t)S->bed);
    snprintf(buf, sizeof buf, "%d\xC2\xB0", (int)lroundf(S->nozzle));
    set_text(t_noz, online() ? buf : "--");
    if (S->nozzle_target > 0) snprintf(buf, sizeof buf, "target %d\xC2\xB0", (int)S->nozzle_target);
    else snprintf(buf, sizeof buf, "nozzle off");
    set_text(t_noz_tgt, buf);
    if (S->bed_target > 0)
        snprintf(buf, sizeof buf, "Bed %d\xC2\xB0 / %d\xC2\xB0", (int)lroundf(S->bed), (int)S->bed_target);
    else
        snprintf(buf, sizeof buf, "Bed %d\xC2\xB0", (int)lroundf(S->bed));
    set_text(t_bed, buf);
    if (S->has_air)
        snprintf(buf, sizeof buf, "Air %d\xC2\xB0  %d%%", (int)lroundf(S->air_temp), (int)lroundf(S->air_hum));
    else
        buf[0] = 0;
    set_text(t_air, buf);
}

void apply_theme() {
    face_set_color(lv_color_hex(O.eyes));
    lv_obj_set_style_arc_color(t_noz_arc, lv_color_hex(O.accent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(t_bed_arc, lv_color_hex(O.second), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(t_noz_tgt, lv_color_hex(O.accent), 0);
    print_apply_theme();
}

// ---------------------------------------------------------------- info overlay

void build_info() {
    info = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(info, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(info, LV_OPA_90, 0);
    lv_obj_remove_flag(info, LV_OBJ_FLAG_CLICKABLE);
    info_lbl = label(info, &lv_font_montserrat_14, COL_TEXT, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(info_lbl, 190);
    lv_label_set_long_mode(info_lbl, LV_LABEL_LONG_WRAP);
    show(info, false);
}

void update_info() {
    char buf[200];
    if (S->ap_mode) {
        snprintf(buf, sizeof buf,
                 "Wi-Fi setup\n\nJoin %s\npassword %s\n\nthen open\nhttp://192.168.4.1", S->ap_ssid,
                 S->ap_pass);
    } else if (now_ms < info_until) {
        snprintf(buf, sizeof buf, "KNOMI-NG %s\n\n%s.local\n%s\n\nprinter %s", O.version,
                 O.hostname, S->ip[0] ? S->ip : "no IP",
                 S->moonraker ? "connected" : "not connected");
    } else {
        show(info, false);
        return;
    }
    set_text(info_lbl, buf);
    show(info, true);
}

// ---------------------------------------------------------------- navigation

void on_scroll_begin(lv_event_t *) {
    manual = true;
    touch_ms = now_ms;
}

bool lanes_on() { return O.lanes_screen && S->afc; }

int current_tile() {
    lv_obj_t *t = lv_tileview_get_tile_active(tv);
    for (int i = 0; i < T_COUNT; i++)
        if (tiles[i] == t) return i;
    return T_FACE;
}

void navigate() {
    if (afc_busy()) afc_busy_until = now_ms + 8000;
    if (lanes_wants_focus()) touch_ms = now_ms;
    if (manual && now_ms - touch_ms > MANUAL_TIMEOUT_MS) manual = false;
    if (manual) return;
    int want = T_FACE;
    if (now_ms < afc_busy_until && lanes_on()) want = T_LANES;
    else if (print_wants_focus()) want = T_PRINT;
    if (current_tile() != want) lv_tileview_set_tile_by_index(tv, want, 0, LV_ANIM_ON);
}

void track_activity() {
    const float targets = S->nozzle_target + S->bed_target;
    const float moved = fabsf(S->pos[0] - last_pos[0]) + fabsf(S->pos[1] - last_pos[1]) +
                        fabsf(S->pos[2] - last_pos[2]);
    if (moved > 0.5f || targets != last_targets || S->busy || job_active() || afc_busy() ||
        heating() || S->job != last_job || !online() || print_wants_focus())
        activity_ms = now_ms;
    memcpy(last_pos, S->pos, sizeof last_pos);
    last_targets = targets;

    if (S->job != last_job) {
        if (S->job == Job::Complete && last_job == Job::Printing) ui_event(UiEvent::JobDone);
        if (S->job == Job::Cancelled && last_job != Job::Cancelled) ui_event(UiEvent::JobCancelled);
        last_job = S->job;
    }
}

}  // namespace

void ui_init(PrinterState *state, const UiOptions &opts) {
    S = state;
    O = opts;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    tv = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(tv, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tv, on_scroll_begin, LV_EVENT_SCROLL_BEGIN, nullptr);
    for (int i = 0; i < T_COUNT; i++) {
        // The last visible tile limits the scroll range itself (the Box
        // Turtle tile is hidden without AFC), so every other tile is HOR.
        lv_dir_t dir = i == 0 ? LV_DIR_RIGHT : LV_DIR_HOR;
        tiles[i] = lv_tileview_add_tile(tv, i, 0, dir);
        lv_obj_set_scrollbar_mode(tiles[i], LV_SCROLLBAR_MODE_OFF);
        // A scaled thumbnail keeps its full layout size; never scroll a tile.
        lv_obj_remove_flag(tiles[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    build_face(tiles[T_FACE]);
    print_build(tiles[T_PRINT]);
    lanes_build(tiles[T_LANES]);
    build_temps(tiles[T_TEMPS]);
    build_info();
    ui_set_options(opts);
}

void ui_set_options(const UiOptions &opts) {
    O = opts;
    apply_theme();
}

void ui_tick(uint32_t now) {
    now_ms = now;
    if (!activity_ms) activity_ms = now;
    show(tiles[T_LANES], lanes_on());
    if (!lanes_on() && current_tile() == T_LANES)
        lv_tileview_set_tile_by_index(tv, T_FACE, 0, LV_ANIM_OFF);
    track_activity();
    navigate();
    const int tile = current_tile();
    update_face();
    face_tick(now);
    if (tile == T_PRINT || print_wants_focus()) print_update();
    if (lanes_on() && (tile == T_LANES || afc_busy())) lanes_update();
    if (tile == T_TEMPS) update_temps();
    update_info();
}

void ui_event(UiEvent ev) {
    switch (ev) {
    case UiEvent::Poke:
    case UiEvent::Bump:
        // Startled, then pleased about the attention.
        surprise_until = now_ms + 700;
        happy_until = now_ms + 2600;
        activity_ms = now_ms;
        break;
    case UiEvent::JobDone:
        happy_until = now_ms + 5 * 60 * 1000;
        print_celebrate();
        break;
    case UiEvent::JobCancelled: sad_until = now_ms + 60 * 1000; break;
    }
}

void ui_touch_activity() {
    touch_ms = now_ms;
    if (now_ms - activity_ms > O.sleep_after_s * 1000u / 2) ui_event(UiEvent::Bump);
    activity_ms = now_ms;
}

void ui_set_thumbnail(const uint8_t *png, size_t len) { print_set_thumbnail(png, len); }

bool ui_asleep() { return face_mood() == Mood::Asleep; }

void ui_show_tile(int index) {
    if (index < 0 || index >= T_COUNT || (index == T_LANES && !lanes_on())) return;
    manual = true;
    touch_ms = now_ms;
    lv_tileview_set_tile_by_index(tv, index, 0, LV_ANIM_OFF);
}
