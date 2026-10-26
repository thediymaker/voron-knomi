// Design mockups: concept screens rendered with the same LVGL build as the
// firmware, written to sim/out/mock/*.ppm (render.py makes PNGs).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "lvgl.h"

static uint16_t fb[240 * 240];
static uint32_t fake_ms = 1;
static std::vector<uint8_t> thumb_png;
static lv_image_dsc_t thumb;

static uint32_t tick_cb() { return fake_ms; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
    const uint16_t *src = (const uint16_t *)px;
    const int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) memcpy(&fb[y * 240 + a->x1], src + (y - a->y1) * w, w * 2);
    lv_display_flush_ready(d);
}

static void snap(const char *name) {
    for (int i = 0; i < 5; i++) {
        fake_ms += 20;
        lv_timer_handler();
    }
    lv_refr_now(nullptr);
    char path[128];
    snprintf(path, sizeof path, "sim/out/mock/%s.ppm", name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("%s\n", path);
}

// ------------------------------------------------------------ draw helpers

static lv_layer_t *L;
static lv_color_t C(uint32_t rgb) { return lv_color_hex(rgb); }

static void rrect(float x1, float y1, float x2, float y2, int32_t r, uint32_t c, lv_opa_t opa = 255,
                  int32_t bw = 0, uint32_t bc = 0) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = C(c);
    d.bg_opa = opa;
    d.radius = r;
    if (bw) {
        d.border_width = bw;
        d.border_color = C(bc);
        d.border_opa = opa;
    }
    lv_area_t a = {(int32_t)lroundf(x1), (int32_t)lroundf(y1), (int32_t)lroundf(x2), (int32_t)lroundf(y2)};
    lv_draw_rect(L, &d, &a);
}

static void circle(float cx, float cy, float r, uint32_t c, lv_opa_t opa = 255, int32_t bw = 0,
                   uint32_t bc = 0) {
    rrect(cx - r, cy - r, cx + r, cy + r, LV_RADIUS_CIRCLE, c, opa, bw, bc);
}

static void ring(float cx, float cy, float r, float w, uint32_t c, lv_opa_t opa = 255) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = 0;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = (int32_t)w;
    d.border_color = C(c);
    d.border_opa = opa;
    lv_area_t a = {(int32_t)(cx - r), (int32_t)(cy - r), (int32_t)(cx + r), (int32_t)(cy + r)};
    lv_draw_rect(L, &d, &a);
}

static void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c, lv_opa_t opa = 255) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.bg_color = C(c);
    d.bg_opa = opa;
    d.p[0] = {x0, y0};
    d.p[1] = {x1, y1};
    d.p[2] = {x2, y2};
    lv_draw_triangle(L, &d);
}

static void line(float x1, float y1, float x2, float y2, int32_t w, uint32_t c, lv_opa_t opa = 255,
                 int32_t dash = 0) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1 = {x1, y1};
    d.p2 = {x2, y2};
    d.width = w;
    d.color = C(c);
    d.opa = opa;
    d.round_start = d.round_end = 1;
    if (dash) {
        d.dash_width = dash;
        d.dash_gap = dash;
    }
    lv_draw_line(L, &d);
}

static void arc(float cx, float cy, float r, int32_t w, float a0, float a1, uint32_t c, lv_opa_t opa = 255,
                bool round = true) {
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center = {(int32_t)cx, (int32_t)cy};
    d.radius = (uint16_t)r;
    d.width = w;
    d.start_angle = a0;
    d.end_angle = a1;
    d.color = C(c);
    d.opa = opa;
    d.rounded = round;
    lv_draw_arc(L, &d);
}

static void text(const char *t, const lv_font_t *f, float cx, float y, uint32_t c, lv_opa_t opa = 255,
                 int32_t w = 200) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = t;
    d.font = f;
    d.color = C(c);
    d.opa = opa;
    d.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t a = {(int32_t)(cx - w / 2), (int32_t)y, (int32_t)(cx + w / 2), (int32_t)y + 80};
    lv_draw_label(L, &d, &a);
}

// Pointy-top hexagon: a rectangle plus a triangle on top and bottom (no
// diagonal seams between shapes).
static void hexagon(float cx, float cy, float r, uint32_t c, lv_opa_t opa = 255) {
    const float hw = r * 0.866f;
    rrect(floorf(cx - hw), floorf(cy - r / 2), ceilf(cx + hw), ceilf(cy + r / 2), 0, c, opa);
    tri(floorf(cx - hw), floorf(cy - r / 2) + 1, ceilf(cx + hw), floorf(cy - r / 2) + 1, cx, cy - r, c, opa);
    tri(floorf(cx - hw), ceilf(cy + r / 2) - 1, ceilf(cx + hw), ceilf(cy + r / 2) - 1, cx, cy + r, c, opa);
}

static bool light(uint32_t rgb) {
    return ((rgb >> 16 & 0xFF) * 299 + (rgb >> 8 & 0xFF) * 587 + (rgb & 0xFF) * 114) > 140000;
}

static const uint32_t ACCENT = 0x38D9F5, GREY = 0x8E8E93, DIM = 0x3A3A3C, RED = 0xFF453A,
                      ORANGE = 0xFF9F0A, BRASS = 0xD4A23A;

struct Spool { const char *tool, *mat; uint32_t color; float grams; };
static const Spool spools[4] = {
    {"T0", "PLA", 0x2F6BFF, 987}, {"T1", "PETG", 0xFF7A1A, 742},
    {"T2", "PETG", 0x48484A, 410}, {"T3", "PETG", 0xF2F2F7, 155}};

// ------------------------------------------------------------ mockups

// Box Turtle as an actual turtle: each lane is a scute of the shell.
static void turtle(bool error) {
    const uint32_t skin = 0x7BC47F, shell = 0x2F5236, shell_dark = 0x1E3523;
    // legs and tail under the shell
    for (int sx : {-1, 1})
        for (int sy : {-1, 1}) {
            const float x = 120 + sx * 82, y = 118 + sy * 50;
            rrect(x - 15, y - 10, x + 15, y + 10, 10, skin, error ? 120 : 255);
        }
    tri(110, 182, 130, 182, 120, 198, skin);
    // head (pulled in when there is a problem)
    if (!error) {
        circle(120, 40, 18, skin);
        circle(112, 36, 3.5f, 0x0B0B0B);
        circle(128, 36, 3.5f, 0x0B0B0B);
        circle(113, 35, 1.2f, 0xFFFFFF);
        circle(129, 35, 1.2f, 0xFFFFFF);
        arc(120, 41, 7, 2, 30, 150, 0x0B0B0B);
    }
    rrect(20, 52, 220, 186, 66, shell, 255, 4, 0x3F6B47);
    // vertebral / costal rows above and below the lane row
    for (int i = 0; i < 3; i++) {
        hexagon(74 + i * 46, 79, 25, shell_dark);
        hexagon(74 + i * 46, 157, 25, shell_dark);
    }
    if (error) {
        // peeking out of the shell
        rrect(96, 52, 144, 66, 7, 0x0B0B0B);
        circle(110, 59, 3, 0xFFFFFF);
        circle(130, 59, 3, 0xFFFFFF);
        circle(120, 30, 13, RED);
        text("!", &lv_font_montserrat_20, 120, 19, 0xFFFFFF);
    }
    for (int i = 0; i < 4; i++) {
        const Spool &s = spools[i];
        const float x = 51 + i * 46, y = 118;
        const bool active = error ? i == 1 : i == 0;
        if (active) hexagon(x, y, 30, error ? RED : ACCENT);
        hexagon(x, y, 25, s.color);
        const uint32_t tc = light(s.color) ? 0x000000 : 0xFFFFFF;
        text(s.tool, &lv_font_montserrat_14, x, y - 13, tc, 255, 44);
        text(s.mat, &lv_font_montserrat_12, x, y + 3, tc, 200, 44);
    }
    if (error) {
        text("T1 stuck in the toolhead", &lv_font_montserrat_12, 120, 198, RED, 255, 170);
    } else {
        text("T0  PLA  987 g", &lv_font_montserrat_14, 120, 200, 0xFFFFFF, 255, 170);
    }
}

static void hotend(float cx, float y, uint32_t fil, bool hot) {
    if (hot) circle(cx, y + 30, 14, ORANGE, 70);
    rrect(cx - 18, y, cx + 18, y + 20, 4, hot ? 0xB8572A : 0x636366);
    rrect(cx - 18, y + 7, cx + 18, y + 11, 0, fil);
    tri(cx - 8, y + 20, cx + 8, y + 20, cx, y + 32, BRASS);
}

// Spools along the top, filament merging at the hub, flowing to the nozzle.
static void spool_flow() {
    const float hub_y = 128;
    for (int i = 0; i < 4; i++) {
        const Spool &s = spools[i];
        const float x = 51 + i * 46;
        const bool active = i == 1;
        line(x, 92, 120, hub_y, active ? 4 : 2, s.color == 0x48484A ? 0x6E6E73 : s.color, active ? 255 : 70);
    }
    // flowing dots along the active strand and down to the nozzle
    for (int k = 0; k < 3; k++) {
        const float t = 0.2f + k * 0.3f;
        circle(97 + (120 - 97) * t, 92 + (hub_y - 92) * t, 3, 0xFFFFFF);
    }
    line(120, hub_y, 120, 160, 4, spools[1].color);
    circle(120, 146, 3, 0xFFFFFF);
    rrect(110, hub_y - 7, 130, hub_y + 7, 4, 0x48484A, 255, 2, 0x636366);
    for (int i = 0; i < 4; i++) {
        const Spool &s = spools[i];
        const float x = 51 + i * 46, y = 70;
        const bool active = i == 1;
        circle(x, y, 21, 0x2C2C2E, 255, active ? 3 : 0, ACCENT);
        circle(x, y, 8 + 12 * fminf(1, s.grams / 1000), s.color);  // filament left = ring size
        circle(x, y, 7, 0x1C1C1E);
        for (int sp = 0; sp < 3; sp++) {
            const float a = (active ? 25 : 0) + sp * 120;
            circle(x + 4.5f * cosf(a * (float)M_PI / 180), y + 4.5f * sinf(a * (float)M_PI / 180), 1.4f, GREY);
        }
        text(s.tool, &lv_font_montserrat_12, x, 36, active ? ACCENT : GREY, 255, 40);
    }
    hotend(120, 160, spools[1].color, true);
    text("238\xC2\xB0", &lv_font_montserrat_14, 166, 165, ORANGE, 255, 50);
    text("Loading T1  PETG", &lv_font_montserrat_14, 120, 204, 0xFFFFFF, 255, 170);
}

static void carriage(float cx, float y, const char *tool, const char *temp, const char *sub, uint32_t fil,
                     bool active) {
    const lv_opa_t o = active ? 255 : 110;
    rrect(cx - 30, y, cx + 30, y + 58, 12, 0x2C2C2E, o, 2, active ? fil : 0x48484A);
    rrect(cx - 30, y + 10, cx - 25, y + 48, 2, fil, o);
    text(tool, &lv_font_montserrat_12, cx + 3, y + 5, active ? 0xFFFFFF : GREY, o, 50);
    text(temp, &lv_font_montserrat_20, cx + 3, y + 20, active ? 0xFFFFFF : GREY, o, 60);
    text(sub, &lv_font_montserrat_12, cx + 3, y + 41, active ? ORANGE : GREY, o, 60);
    if (active) circle(cx, y + 74, 12, ORANGE, 60);
    tri(cx - 8, y + 58, cx + 8, y + 58, cx, y + 70, BRASS, o);
}

// IDEX: two carriages on the X rail; split rim = each tool's heat.
static void idex(bool mirror) {
    arc(120, 120, 114, 5, 95, 265, 0x3A1F0A, 255, false);
    arc(120, 120, 114, 5, 275, 445, 0x0A2A4A, 255, false);
    arc(120, 120, 114, 5, 95, mirror ? 265 : 265, ORANGE, 255, false);
    arc(120, 120, 114, 5, 275, mirror ? 445 : 380, 0x0A84FF, 255, false);
    line(26, 62, 214, 62, 4, 0x48484A);
    if (mirror) {
        carriage(78, 70, "T0", "240\xC2\xB0", "printing", spools[1].color, true);
        carriage(162, 70, "T1", "240\xC2\xB0", "printing", spools[1].color, true);
        line(120, 72, 120, 150, 1, GREY, 255, 3);
        rrect(78, 162, 162, 184, 11, ACCENT);
        text("MIRROR", &lv_font_montserrat_14, 120, 165, 0x001318, 255, 84);
        text("2 parts  42%", &lv_font_montserrat_12, 120, 192, GREY, 255, 150);
    } else {
        carriage(84, 70, "T0", "215\xC2\xB0", "printing", spools[0].color, true);
        carriage(178, 70, "T1", "170\xC2\xB0", "parked", spools[1].color, false);
        text("42%", &lv_font_montserrat_28, 120, 156, 0xFFFFFF, 255, 100);
        text("swap to T1 at layer 58", &lv_font_montserrat_12, 120, 190, GREY, 255, 160);
    }
}

// Each eye is a hotend: filament colour, glow when hot, lid when parked.
static void hotend_eyes() {
    const float y1 = 80, y2 = 146;
    circle(81, 113, 50, spools[0].color, 40);
    rrect(52, y1, 110, y2, 18, spools[0].color);
    rrect(130, y1, 188, y2, 18, spools[1].color, 150);
    rrect(124, y1 - 10, 194, y1 + 34, 0, 0x000000);  // half-closed: parked
    text("Z", &lv_font_montserrat_20, 196, 56, spools[1].color, 160, 30);
    text("z", &lv_font_montserrat_14, 184, 74, spools[1].color, 120, 30);
    text("T0  215\xC2\xB0", &lv_font_montserrat_14, 81, 160, 0xFFFFFF, 255, 90);
    text("T1  170\xC2\xB0", &lv_font_montserrat_14, 159, 160, GREY, 255, 90);
}

// Googly eyes driven by the accelerometer: pupils swing with the toolhead.
static void googly() {
    for (int i = 0; i < 2; i++) {
        const float cx = i ? 166 : 74, cy = 118;
        circle(cx, cy, 44, 0xF5F5F7, 255, 3, 0x2C2C2E);
        const float px = cx - 17 + i * 3, py = cy + 15 + i * 2;
        circle(px, py, 20, 0x0B0B0B);
        circle(px + 7, py - 7, 4, 0xFFFFFF);
    }
    for (int k = 0; k < 3; k++) line(206, 92 + k * 26, 226, 92 + k * 26, 3, GREY, 160);
}

static void confetti() {
    srand(7);
    const uint32_t cols[] = {0xFF453A, 0xFF9F0A, 0xFFD60A, 0x30D158, 0x38D9F5, 0xBF5AF2};
    for (int i = 0; i < 46; i++) {
        const float a = (rand() % 360) * (float)M_PI / 180, r = 60 + rand() % 52;
        const float x = 120 + r * cosf(a), y = 120 + r * sinf(a);
        if ((y > 168 || y < 62) && fabsf(x - 120) < 76) continue;
        const uint32_t c = cols[rand() % 6];
        if (rand() % 2) circle(x, y, 2.5f, c);
        else rrect(x - 4, y - 1.5f, x + 4, y + 1.5f, 1, c);
    }
}

static void done_screen() {
    confetti();
    text("Done!", &lv_font_montserrat_28, 120, 22, 0xFFFFFF, 255, 140);
    text("1h 12m   41 g   PLA", &lv_font_montserrat_14, 120, 190, GREY, 255, 170);
}

static void heat_soak() {
    arc(120, 120, 108, 10, 0, 360, ORANGE, 50, false);
    arc(120, 120, 108, 10, 270, 270 + 360 * 0.62f, ORANGE);
    text("Heat soak", &lv_font_montserrat_14, 120, 56, GREY, 255, 120);
    text("07:42", &lv_font_montserrat_48, 120, 82, 0xFFFFFF, 255, 180);
    text("chamber 41\xC2\xB0  \xE2\x80\xA2  50\xC2\xB0", &lv_font_montserrat_14, 120, 142, ORANGE, 255, 180);
    // tiny sleepy eyes waiting
    rrect(98, 176, 114, 180, 2, ACCENT, 200);
    rrect(126, 176, 142, 180, 2, ACCENT, 200);
}

static void quick_menu() {
    circle(120, 120, 46, 0x1C1C1E, 255, 2, 0x2C2C2E);
    text("42%", &lv_font_montserrat_28, 120, 96, 0xFFFFFF, 255, 90);
    text("T0  layer 57", &lv_font_montserrat_12, 120, 128, GREY, 255, 90);
    struct B { float x, y; const char *icon, *label; uint32_t c; };
    const B b[4] = {{120, 40, LV_SYMBOL_PAUSE, "Pause", ACCENT},
                    {200, 120, LV_SYMBOL_CHARGE, "Light", 0xFFD60A},
                    {120, 200, LV_SYMBOL_CLOSE, "Cancel", RED},
                    {40, 120, LV_SYMBOL_LOOP, "Swap", 0x30D158}};
    for (const B &x : b) {
        circle(x.x, x.y, 30, 0x1C1C1E, 255, 3, x.c);
        text(x.icon, &lv_font_montserrat_20, x.x, x.y - 18, x.c, 255, 50);
        text(x.label, &lv_font_montserrat_12, x.x, x.y + 4, 0xFFFFFF, 255, 56);
    }
}

static void night_clock() {
    text("23:41", &lv_font_montserrat_48, 120, 62, 0xFFFFFF, 230, 200);
    text("Mon 26 Oct", &lv_font_montserrat_14, 120, 118, GREY, 255, 150);
    rrect(70, 158, 112, 163, 3, ACCENT, 150);
    rrect(128, 158, 170, 163, 3, ACCENT, 150);
    text("z", &lv_font_montserrat_14, 180, 138, ACCENT, 120, 20);
    text("Z", &lv_font_montserrat_20, 192, 120, ACCENT, 150, 24);
    text("bed 24\xC2\xB0   idle 2h", &lv_font_montserrat_12, 120, 188, GREY, 255, 150);
}

// ------------------------------------------------------------ driver

typedef void (*DrawFn)();
static DrawFn current;

static void draw_cb(lv_event_t *e) {
    L = lv_event_get_layer(e);
    current();
}

static void render(const char *name, DrawFn fn, bool with_thumb = false) {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    if (with_thumb) {
        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, &thumb);
        lv_image_set_scale(img, 128 * 256 / 300);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 240, 240);
    current = fn;
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_screen_load(scr);
    snap(name);
}

int main() {
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(240, 240);
    static uint16_t buf[240 * 240];
    lv_display_set_buffers(d, buf, nullptr, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);

    FILE *tf = fopen("sim/assets/thumb.png", "rb");
    fseek(tf, 0, SEEK_END);
    thumb_png.resize(ftell(tf));
    fseek(tf, 0, SEEK_SET);
    fread(thumb_png.data(), 1, thumb_png.size(), tf);
    fclose(tf);
    thumb.header.magic = LV_IMAGE_HEADER_MAGIC;
    thumb.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    thumb.header.w = thumb.header.h = 300;
    thumb.data = thumb_png.data();
    thumb.data_size = thumb_png.size();

    render("a-turtle", [] { turtle(false); });
    render("b-turtle-error", [] { turtle(true); });
    render("c-spool-flow", spool_flow);
    render("d-idex", [] { idex(false); });
    render("e-idex-mirror", [] { idex(true); });
    render("f-hotend-eyes", hotend_eyes);
    render("g-googly", googly);
    render("h-done", done_screen, true);
    render("i-heat-soak", heat_soak);
    render("j-quick-menu", quick_menu);
    render("k-night-clock", night_clock);
    return 0;
}
