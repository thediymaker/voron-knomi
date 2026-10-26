// Box Turtle screen. Lanes sit on the upper half of the ring, each a segment
// filled in its filament colour as far as the spool is full. In the middle is
// the turtle: its shell plates are the lanes, it walks while AFC moves
// filament and hides when AFC reports an error. Tapping a lane (or the turtle)
// opens commands; each needs a second tap to run, and they are locked while
// printing.
#include <initializer_list>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

namespace ui {
namespace {

const float CX = 120, CY = 120, R = 104;  // lane ring
const int32_t RING_W = 16;
const float TX = 120, TY = 104;           // turtle centre
const uint32_t SKIN = 0x7BC47F, SHELL = 0x2F5236, SHELL_RIM = 0x46774F, SHELL_DARK = 0x1E3523;
const int MAX_SHOWN = 4;
const int MENU = 100;

lv_obj_t *canvas, *line1, *line2, *btn[2], *btn_lbl[2];
int sel = -1;            // selected lane, MENU for the unit menu
uint32_t sel_ms, armed_ms, sent_until;
int armed = -1;          // button waiting for its confirming tap
char sent_msg[48];

struct Action {
    char label[16];
    char gcode[64];
    char done[40];  // shown after sending
    bool confirm;
};
Action acts[2];
int act_count;

int shown() { return S->lane_count < MAX_SHOWN ? S->lane_count : MAX_SHOWN; }

float seg_deg() { return 180.0f / (shown() ? shown() : 1); }

bool lane_full(const Lane &L) { return L.prep || L.load || L.tool_loaded; }

float lane_fill(const Lane &L) {
    if (L.weight <= 0) return 1;
    const float full = L.initial_weight > 0 ? L.initial_weight : 1000;
    return fmaxf(0.06f, fminf(1, L.weight / full));
}

uint32_t lane_col(const Lane &L) { return L.has_color ? L.color : 0x9A9AA0; }

// Black filament vanishes on the black screen and dark shell: give it an edge.
bool lane_dark(const Lane &L) {
    const uint32_t c = lane_col(L);
    return ((c >> 16 & 0xFF) * 299 + (c >> 8 & 0xFF) * 587 + (c & 0xFF) * 114) < 45000;
}

int moving_lane() { return afc_busy() && !S->afc_error ? lane_index(*S, S->afc_moving) : -1; }

void lane_title(char *out, size_t n, const Lane &L) {
    if (!lane_full(L)) {
        snprintf(out, n, "%s  empty", L.map[0] ? L.map : L.name);
    } else if (L.weight > 0) {
        snprintf(out, n, "%s  %s  %d g", L.map[0] ? L.map : L.name, L.material[0] ? L.material : "?",
                 (int)(L.weight + 0.5f));
    } else {
        snprintf(out, n, "%s  %s", L.map[0] ? L.map : L.name, L.material[0] ? L.material : "?");
    }
}

void text_at(lv_layer_t *l, const char *t, const lv_font_t *f, float cx, float cy, uint32_t c) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = t;
    d.font = f;
    d.color = lv_color_hex(c);
    d.align = LV_TEXT_ALIGN_CENTER;
    const int32_t h = lv_font_get_line_height(f);
    lv_area_t a = {(int32_t)cx - 30, (int32_t)(cy - h / 2.0f), (int32_t)cx + 30, (int32_t)(cy + h / 2.0f)};
    lv_draw_label(l, &d, &a);
}

void draw_turtle(lv_layer_t *l, float ox, float oy) {
    const float x = ox + TX, y = oy + TY;
    const bool hide = S->afc_error;
    const bool walk = moving_lane() >= 0;
    const float step = walk ? sinf(now_ms / 110.0f) * 3 : 0;
    // legs (diagonal pairs move together), tail
    for (int sx : {-1, 1})
        for (int sy : {-1, 1}) {
            const float dy = (sx == sy ? step : -step);
            const float lx = x + sx * 47, ly = y + sy * 20 + dy;
            draw_rect(l, lx - 10, ly - 7, lx + 10, ly + 7, 7, SKIN, hide ? LV_OPA_40 : LV_OPA_COVER);
        }
    draw_tri(l, x - 6, y + 30, x + 6, y + 30, x, y + 41, SKIN);
    // head
    if (!hide) {
        const float hy = y - 40 + (walk ? fabsf(step) * 0.5f : 0);
        draw_circle(l, x, hy, 12, SKIN);
        const bool blink = (now_ms % 4200) < 140;
        for (int s : {-1, 1}) {
            if (blink) {
                draw_rect(l, x + s * 5 - 2.5f, hy - 3, x + s * 5 + 2.5f, hy - 2, 1, 0x0B0B0B);
            } else {
                draw_circle(l, x + s * 5, hy - 3, 2.6f, 0x0B0B0B);
                draw_circle(l, x + s * 5 + 0.8f, hy - 3.8f, 0.9f, 0xFFFFFF);
            }
        }
    }
    // shell
    draw_rect(l, x - 54, y - 33, x + 54, y + 33, 33, SHELL_RIM);
    draw_rect(l, x - 51, y - 30, x + 51, y + 30, 30, SHELL);
    for (int j = 0; j < 3; j++) {
        draw_hex(l, x + (j - 1) * 25, y - 19, 10, SHELL_DARK);
        draw_hex(l, x + (j - 1) * 25, y + 21, 10, SHELL_DARK);
    }
    const int n = shown();
    const int mv = moving_lane();
    for (int i = 0; i < n; i++) {
        const Lane &L = S->lanes[i];
        const float hx = x + (i - (n - 1) / 2.0f) * 25, hy = y + 1;
        const bool hot = L.tool_loaded || i == mv || i == sel;
        if (hot) draw_hex(l, hx, hy, 15.5f, i == sel ? 0xFFFFFF : O.accent);
        else if (lane_full(L) && lane_dark(L)) draw_hex(l, hx, hy, 14.5f, 0x6E6E73);
        draw_hex(l, hx, hy, 13, lane_full(L) ? lane_col(L) : SHELL_DARK);
    }
    if (hide) {
        // peeking out of the shell
        draw_rect(l, x - 13, y - 37, x + 13, y - 27, 5, 0x0B0B0B);
        draw_circle(l, x - 5, y - 32, 2.2f, 0xFFFFFF);
        draw_circle(l, x + 5, y - 32, 2.2f, 0xFFFFFF);
        draw_circle(l, x, y - 52, 10, COL_RED);
        text_at(l, "!", &lv_font_montserrat_14, x, y - 52, 0xFFFFFF);
    }
}

void canvas_draw(lv_event_t *e) {
    lv_layer_t *l = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(canvas, &a);
    const float ox = a.x1, oy = a.y1;
    const int n = shown();
    const float seg = seg_deg();
    const int mv = moving_lane();
    const int err_lane = S->afc_error ? lane_index(*S, S->afc_moving[0] ? S->afc_moving : S->afc_lane) : -1;
    const float pulse = 0.5f + 0.5f * sinf(now_ms / 180.0f);

    for (int i = 0; i < n; i++) {
        const Lane &L = S->lanes[i];
        const float a0 = 180 + i * seg + 3, a1 = 180 + (i + 1) * seg - 3;
        const float mid = (a0 + a1) / 2;
        if (lane_full(L)) {
            const uint32_t c = lane_col(L);
            if (lane_dark(L)) draw_arc(l, ox + CX, oy + CY, R + 1, RING_W + 2, a0, a1, 0x5A5A5E, LV_OPA_COVER, false);
            draw_arc(l, ox + CX, oy + CY, R, RING_W, a0, a1, c, LV_OPA_30, false);
            draw_arc(l, ox + CX, oy + CY, R, RING_W, a0, a0 + (a1 - a0) * lane_fill(L), c,
                     LV_OPA_COVER, false);
        } else {
            draw_arc(l, ox + CX, oy + CY, R - RING_W / 2 + 2, 3, a0, a1, COL_DIM, LV_OPA_COVER, false);
        }
        uint32_t mark = 0;
        lv_opa_t mark_opa = LV_OPA_COVER;
        if (i == err_lane) { mark = COL_RED; mark_opa = (lv_opa_t)(120 + 135 * pulse); }
        else if (i == sel) mark = 0xFFFFFF;
        else if (L.tool_loaded || i == mv) mark = O.accent;
        if (mark) draw_arc(l, ox + CX, oy + CY, R + 13, 3, a0, a1, mark, mark_opa, true);
        const float rad = mid * (float)M_PI / 180;
        text_at(l, L.map[0] ? L.map : L.name, &lv_font_montserrat_14, ox + CX + 80 * cosf(rad),
                oy + CY + 80 * sinf(rad), lane_full(L) ? COL_TEXT : COL_MUTED);
    }

    // Filament flowing between the spool and the turtle.
    if (mv >= 0 && mv < n) {
        const float mid = (180 + (mv + 0.5f) * seg) * (float)M_PI / 180;
        const float x0 = ox + CX + (R - 12) * cosf(mid), y0 = oy + CY + (R - 12) * sinf(mid);
        const float x1 = ox + TX, y1 = oy + TY - 10;
        const bool out = strstr(S->afc_state, "nload") || strstr(S->afc_state, "ject");
        const uint32_t c = lane_col(S->lanes[mv]);
        for (int k = 0; k < 4; k++) {
            float p = fmodf(now_ms / 900.0f + k / 4.0f, 1.0f);
            if (out) p = 1 - p;
            draw_circle(l, x0 + (x1 - x0) * p, y0 + (y1 - y0) * p, 4, c);
            draw_circle(l, x0 + (x1 - x0) * p, y0 + (y1 - y0) * p, 1.5f, 0xFFFFFF);
        }
    }
    draw_turtle(l, ox, oy);
}

void close_sel() {
    sel = -1;
    armed = -1;
}

void run_action(int i) {
    if (i < 0 || i >= act_count) return;
    if (acts[i].confirm && armed != i) {
        armed = i;
        armed_ms = now_ms;
        return;
    }
    send_gcode(acts[i].gcode);
    snprintf(sent_msg, sizeof sent_msg, "%s", acts[i].done);
    sent_until = now_ms + 3500;
    close_sel();
}

void on_btn(lv_event_t *e) {
    sel_ms = now_ms;
    run_action((int)(intptr_t)lv_event_get_user_data(e));
}

void on_tile_click(lv_event_t *) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(canvas, &a);
    const float dx = p.x - a.x1 - CX, dy = p.y - a.y1 - CY;
    const float r = sqrtf(dx * dx + dy * dy);
    float ang = atan2f(dy, dx) * 180 / (float)M_PI;
    if (ang < 0) ang += 360;
    sel_ms = now_ms;
    armed = -1;
    if (r > 84 && ang >= 180 && shown() > 0) {
        const int i = (int)((ang - 180) / seg_deg());
        sel = (sel == i) ? -1 : (i < shown() ? i : -1);
    } else if (fabsf(p.x - a.x1 - TX) < 56 && fabsf(p.y - a.y1 - TY) < 46) {
        sel = sel == MENU ? -1 : MENU;
    } else {
        close_sel();
    }
}

void set_actions() {
    act_count = 0;
    auto add = [](const char *label, const char *done, bool confirm, const char *fmt,
                  const char *arg = "") {
        Action &a = acts[act_count++];
        snprintf(a.label, sizeof a.label, "%s", label);
        snprintf(a.done, sizeof a.done, "%s", done);
        snprintf(a.gcode, sizeof a.gcode, fmt, arg);
        a.confirm = confirm;
    };
    if (job_active() || !online()) return;
    if (sel == MENU) {
        add(S->afc_led ? "Light off" : "Light on", S->afc_led ? "Light off" : "Light on", false,
            S->afc_led ? "TURN_OFF_AFC_LED" : "TURN_ON_AFC_LED");
        if (S->afc_error) add("Reset", "Resetting the failure", true, "RESET_FAILURE");
        else if (S->afc_lane[0]) add("Unload", "Unloading the toolhead", true, "TOOL_UNLOAD");
    } else if (sel >= 0 && sel < S->lane_count) {
        const Lane &L = S->lanes[sel];
        const char *t = L.map[0] ? L.map : L.name;
        char done[40];
        if (L.tool_loaded) {
            snprintf(done, sizeof done, "Unloading %s", t);
            add("Unload", done, true, "TOOL_UNLOAD");
        } else if (lane_full(L)) {
            snprintf(done, sizeof done, "Loading %s", t);
            add("Load", done, true, "CHANGE_TOOL LANE=%s", L.name);
            snprintf(done, sizeof done, "Ejecting %s", t);
            add("Eject", done, true, "LANE_UNLOAD LANE=%s", L.name);
        }
    }
}

}  // namespace

void lanes_build(lv_obj_t *t) {
    canvas = lv_obj_create(t);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_size(canvas, 240, 240);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas, canvas_draw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(t, on_tile_click, LV_EVENT_SHORT_CLICKED, nullptr);

    line1 = label(t, &lv_font_montserrat_14, COL_TEXT, LV_ALIGN_TOP_MID, 0, 148);
    line2 = label(t, &lv_font_montserrat_12, COL_MUTED, LV_ALIGN_TOP_MID, 0, 168);
    lv_obj_set_width(line2, 156);
    lv_label_set_long_mode(line2, LV_LABEL_LONG_WRAP);
    for (int i = 0; i < 2; i++) {
        btn[i] = lv_button_create(t);
        lv_obj_set_size(btn[i], 68, 28);
        lv_obj_set_style_radius(btn[i], 14, 0);
        lv_obj_set_style_shadow_width(btn[i], 0, 0);
        lv_obj_set_style_bg_color(btn[i], lv_color_hex(0x2C2C2E), 0);
        lv_obj_set_style_border_width(btn[i], 2, 0);
        btn_lbl[i] = label(btn[i], &lv_font_montserrat_14, COL_TEXT, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_event_cb(btn[i], on_btn, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        show(btn[i], false);
    }
}

bool lanes_wants_focus() { return sel >= 0; }

void lanes_update() {
    if (sel >= 0 && now_ms - sel_ms > 12000) close_sel();
    if (armed >= 0 && now_ms - armed_ms > 4000) armed = -1;
    if (sel >= 0 && sel != MENU && sel >= S->lane_count) close_sel();
    set_actions();

    char a[96], b[192];
    uint32_t b_col = COL_MUTED;
    const int mv = moving_lane();
    if (!online() || !S->afc || S->lane_count == 0) {
        snprintf(a, sizeof a, "%s", online() ? "No Box Turtle" : "Printer offline");
        b[0] = 0;
    } else if (sel == MENU) {
        snprintf(a, sizeof a, "Box Turtle");
        snprintf(b, sizeof b, "%s", job_active() ? "Controls are locked while printing"
                                                 : (S->afc_led ? "Light is on" : "Light is off"));
    } else if (sel >= 0) {
        const Lane &L = S->lanes[sel];
        lane_title(a, sizeof a, L);
        snprintf(b, sizeof b, "%s", job_active() ? "Controls are locked while printing"
                                                 : (L.status[0] ? L.status : ""));
    } else {
        const int loaded = lane_index(*S, S->afc_lane);
        if (loaded >= 0) lane_title(a, sizeof a, S->lanes[loaded]);
        else snprintf(a, sizeof a, "Nothing loaded");
        if (S->afc_error) {
            snprintf(b, sizeof b, "%s", S->afc_msg[0] ? S->afc_msg : "AFC error");
            b_col = COL_RED;
        } else if (now_ms < sent_until) {
            snprintf(b, sizeof b, "%s", sent_msg);
            b_col = O.accent;
        } else if (mv >= 0) {
            const Lane &L = S->lanes[mv];
            snprintf(b, sizeof b, "%s %s", S->afc_state, L.map[0] ? L.map : L.name);
            b_col = O.accent;
        } else if (S->toolchanges > 0 && job_active()) {
            snprintf(b, sizeof b, "Tool change %d of %d", S->toolchange, S->toolchanges);
        } else {
            snprintf(b, sizeof b, "Tap a spool or the turtle");
        }
    }
    set_text(line1, a);
    set_text(line2, b);
    lv_obj_set_style_text_color(line2, lv_color_hex(b_col), 0);
    lv_obj_align(line2, LV_ALIGN_TOP_MID, 0, S->afc_error && sel < 0 ? 166 : 168);

    for (int i = 0; i < 2; i++) {
        const bool on = i < act_count && sel >= 0;
        show(btn[i], on);
        if (!on) continue;
        const bool arm = armed == i;
        set_text(btn_lbl[i], arm ? "Sure?" : acts[i].label);
        lv_obj_set_style_bg_color(btn[i], lv_color_hex(arm ? O.accent : 0x2C2C2E), 0);
        lv_obj_set_style_border_color(btn[i], lv_color_hex(O.accent), 0);
        lv_obj_set_style_text_color(btn_lbl[i], lv_color_hex(arm ? 0x000000 : COL_TEXT), 0);
        const int32_t x = act_count == 1 ? 0 : (i == 0 ? -37 : 37);
        lv_obj_align(btn[i], LV_ALIGN_TOP_MID, x, 190);
    }
    lv_obj_invalidate(canvas);
}

}  // namespace ui
