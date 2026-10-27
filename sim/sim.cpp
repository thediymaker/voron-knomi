// Desktop simulator: renders the UI for scripted printer states into
// 240x240 PPM frames (sim/out/*.ppm); render.py turns them into PNGs.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "lvgl.h"
#include "theme.h"
#include "ui.h"

static uint32_t fake_ms = 1;
static uint16_t fb[240 * 240];
static PrinterState S;
static std::vector<uint8_t> thumb;
static UiOptions opts;

static uint32_t tick_cb() { return fake_ms; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    const uint16_t *src = (const uint16_t *)px;
    const int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++)
        memcpy(&fb[y * 240 + a->x1], src + (y - a->y1) * w, w * 2);
    lv_display_flush_ready(d);
}

static void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
        fake_ms += 20;
        ui_tick(fake_ms);
        lv_timer_handler();
    }
}

static void snap(const char *name) {
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);
    char path[128];
    snprintf(path, sizeof path, "sim/out/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2),
                          (uint8_t)((c & 0x1F) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("%s\n", path);
}

static void gcode(const char *script) { printf("    gcode: %s\n", script); }

static bool touch_down;
static lv_point_t touch_pt;
static void touch_read(lv_indev_t *, lv_indev_data_t *d) {
    d->point = touch_pt;
    d->state = touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void tap(int x, int y) {
    touch_pt = {x, y};
    touch_down = true;
    run(80);
    touch_down = false;
    run(120);
}

static void lane(int i, const char *map, const char *mat, uint32_t color, bool full, float g) {
    Lane &L = S.lanes[i];
    snprintf(L.name, sizeof L.name, "lane%d", i + 1);
    snprintf(L.map, sizeof L.map, "%s", map);
    snprintf(L.material, sizeof L.material, "%s", mat);
    snprintf(L.status, sizeof L.status, "%s", full ? "Loaded" : "None");
    L.color = color;
    L.has_color = color != 0;
    L.prep = L.load = full;
    L.weight = g;
    L.initial_weight = 1000;
}

static void base_state() {
    S = PrinterState();
    S.wifi = true;
    S.moonraker = true;
    strcpy(S.ip, "192.168.1.50");
    S.klippy = Klippy::Ready;
    S.nozzle = 26; S.bed = 25;
    strcpy(S.homed, "xyz");
    S.pos[0] = 175; S.pos[1] = 175; S.pos[2] = 10;
    S.afc = true;
    S.lane_count = 4;
    lane(0, "T0", "PLA", 0x2F6BFF, true, 987);
    lane(1, "T1", "PETG", 0xFF7A1A, true, 742);
    lane(2, "T2", "PETG", 0x1C1C1E, true, 410);
    lane(3, "T3", "PETG", 0xF2F2F7, true, 155);
    S.lanes[0].tool_loaded = true;
    snprintf(S.lanes[0].status, sizeof S.lanes[0].status, "Tooled");
    strcpy(S.afc_lane, "lane1");
    strcpy(S.afc_state, "Idle");
    S.time_ok = true; S.hour = 23; S.minute = 41;
    strcpy(S.date, "Monday 26 October");
}

static void set_msg(const char *m) {
    snprintf(S.display_msg, sizeof S.display_msg, "%s", m);
    S.display_msg_ms = fake_ms;
}

static void theme(const char *id, uint32_t eyes = COL_WHITE_EYES) {
    const ThemePreset &t = theme_find(id);
    opts.accent = t.accent;
    opts.second = t.second;
    opts.eyes = eyes;
    ui_set_options(opts);
}

static void printing() {
    base_state();
    S.job = Job::Printing;
    S.busy = true;
    strcpy(S.filename, "[a]_box_turtle_PLA_1h15m.gcode");
    S.progress = 0.42f; S.print_duration = 1900; S.slicer_estimate = 4500;
    S.layer = 57; S.layers = 210;
    S.nozzle = 215; S.nozzle_target = 215; S.bed = 60; S.bed_target = 60;
    set_msg("May the Layer Adhesion Be With You");
}

int main() {
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(240, 240);
    static uint16_t buf[240 * 240];
    lv_display_set_buffers(d, buf, nullptr, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    lv_indev_t *ind = lv_indev_create();
    lv_indev_set_type(ind, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ind, touch_read);

    FILE *tf = fopen("sim/assets/thumb.png", "rb");
    if (tf) {
        fseek(tf, 0, SEEK_END);
        thumb.resize(ftell(tf));
        fseek(tf, 0, SEEK_SET);
        fread(thumb.data(), 1, thumb.size(), tf);
        fclose(tf);
    }

    base_state();
    strcpy(opts.version, "0.2.0");
    opts.gcode = gcode;
    ui_init(&S, opts);
    theme("neon");
    run(1500);
    snap("01-idle-neon");

    // Print start: stages from PRINT_START's SET_DISPLAY_TEXT.
    base_state();
    S.job = Job::Printing; S.busy = true;
    strcpy(S.filename, "[a]_box_turtle_PLA_1h15m.gcode");
    S.bed = 71; S.bed_target = 110; S.nozzle = 26;
    set_msg("Bed: 110c");
    run(1200);
    snap("02-start-bed");
    ui_set_thumbnail(thumb.data(), thumb.size());
    ui_show_tile(1);
    run(300);
    snap("03-start-print-screen");
    ui_show_tile(0);
    S.bed = 110; S.nozzle = 150; S.nozzle_target = 150;
    set_msg("Soak for 1 min");
    run(18000);
    snap("04-soak");

    printing();
    run(31000);  // manual-swipe timeout
    set_msg("May the Layer Adhesion Be With You");  // sayings show for 20 s
    run(1000);
    snap("05-printing-saying");
    ui_show_tile(1);
    run(300);
    snap("06-print");
    ui_show_tile(2);
    run(300);
    snap("07-temps");

    // Finished: confetti, then it waits for a tap.
    ui_show_tile(0);
    run(31000);
    S.job = Job::Complete; S.progress = 1; S.print_duration = 4380; S.filament_used = 12400;
    run(900);
    snap("08-done-confetti");
    run(6000);
    snap("09-done-waiting");

    // Box Turtle ring.
    base_state();
    ui_show_tile(3);
    run(400);
    snap("10-lanes-idle");
    strcpy(S.afc_state, "Loading");
    strcpy(S.afc_moving, "lane2");
    S.lanes[0].tool_loaded = false;
    strcpy(S.afc_lane, "");
    run(500);
    snap("11-lanes-loading");
    S.afc_error = true;
    strcpy(S.afc_state, "Error");
    strcpy(S.afc_msg, "lane2: filament stuck in toolhead");
    run(500);
    snap("12-lanes-error");

    // Two Box Turtles: 8 lanes, T5 loading, then T6 tapped.
    base_state();
    S.lane_count = 8;
    lane(4, "T4", "ABS", 0xE5E5EA, true, 820);
    lane(5, "T5", "ASA", 0xFF2D55, true, 640);
    lane(6, "T6", "PLA", 0x34C759, true, 300);
    lane(7, "T7", "", 0, false, -1);
    ui_show_tile(3);
    strcpy(S.afc_state, "Loading");
    strcpy(S.afc_moving, "lane6");
    run(500);
    snap("12b-lanes-8");
    strcpy(S.afc_state, "Idle");
    strcpy(S.afc_moving, "");
    run(200);
    tap(120 + 96 * cosf(112.5f * 3.14159f / 180), 120 + 96 * sinf(112.5f * 3.14159f / 180));
    snap("12c-lanes-8-T6-selected");

    // Tap lane T2 (upper right), arm Load, confirm it; then the turtle menu.
    base_state();
    ui_show_tile(3);
    run(300);
    tap(160, 24);
    snap("13-lanes-T2-selected");
    tap(120, 81);
    snap("13b-lanes-T2-armed");
    tap(120, 81);
    run(200);
    snap("13c-lanes-sent");
    tap(120, 104);
    snap("13d-lanes-menu");
    tap(20, 200);

    // Night clock.
    base_state();
    ui_show_tile(0);
    run(31000 + 600 * 1000);
    snap("14-night-clock");

    // Other themes and eye colours.
    base_state();
    printing();
    theme("cyan", 0x38D9F5);
    run(800);
    snap("15-theme-cyan");
    theme("magenta");
    ui_show_tile(2);
    run(300);
    snap("16-theme-magenta-temps");
    theme("amber");
    ui_show_tile(1);
    ui_set_thumbnail(thumb.data(), thumb.size());
    run(300);
    snap("17-theme-amber-print");
    return 0;
}
