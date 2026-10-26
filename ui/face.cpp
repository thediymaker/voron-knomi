// Procedurally drawn robot eyes: two glowing rounded rectangles shaped by
// eyelids (top lid with a tilt, bottom lid, and a "happy" crescent cut from
// below). Every shape parameter eases toward the current mood's target, so
// moods blend into each other instead of switching frames like the stock GIFs.
#include "face.h"
#include <stdlib.h>

namespace {

struct Eye {
    float w, h, r;  // size and corner radius
    float lid_top;  // 0..1 of the height covered from the top
    float tilt;     // top lid slope in degrees; > 0 drops the inner corner
    float lid_bot;  // 0..1 covered from the bottom
    float happy;    // 0..1 crescent cut from below
    float dy;       // vertical offset
};

const float EYE_GAP = 24;  // between the eyes
const float EYE_Y = 112;   // eye centre line
const float LOOK_X = 28, LOOK_Y = 22;
const uint32_t RED = 0xFF453A, AMBER = 0xFFB020;

lv_obj_t *obj;
Mood mood = Mood::Neutral;
Eye cur[2], tgt[2];
float look_x, look_y, look_tx, look_ty;
bool look_free = true;
float col[3], col_t[3];
uint32_t accent = 0x38D9F5;
float blink = 1;             // eye height factor while blinking
uint32_t blink_at, blink_start;
uint32_t wander_at;
uint32_t last_ms;
bool question, night;
float night_k;               // 0..1, eased
float anim_t;                // seconds, drives the zzz and "?"

void shape_for(Mood m, Eye out[2]) {
    const Eye n = {58, 66, 18, 0, 0, 0, 0, 0};
    out[0] = out[1] = n;
    for (int i = 0; i < 2; i++) {
        Eye &e = out[i];
        switch (m) {
        case Mood::Neutral: break;
        case Mood::Happy: e.h = 62; e.happy = 0.52f; e.dy = -2; break;
        case Mood::Focused: e.h = 60; e.lid_top = 0.2f; e.tilt = 5; break;
        case Mood::Sleepy: e.lid_top = 0.52f; e.lid_bot = 0.08f; e.dy = 6; break;
        case Mood::Asleep: e.lid_top = 0.9f; e.lid_bot = 0.06f; e.dy = 12; break;
        case Mood::Surprised: e.w = 64; e.h = 78; e.r = 32; e.dy = -4; break;
        case Mood::Angry: e.h = 62; e.lid_top = 0.16f; e.tilt = 22; break;
        case Mood::Sad: e.h = 58; e.lid_top = 0.3f; e.tilt = -18; e.dy = 8; break;
        case Mood::Squint: e.h = 60; e.lid_top = 0.42f; e.lid_bot = 0.28f; break;
        case Mood::Worried: e.w = 54; e.h = 62; e.lid_top = 0.2f; e.tilt = -14; break;
        case Mood::Confused:
            if (i == 0) { e.h = 54; e.lid_top = 0.36f; e.tilt = -8; }
            else { e.w = 62; e.h = 74; e.r = 24; e.dy = -6; }
            break;
        }
    }
}

uint32_t color_for(Mood m) {
    if (m == Mood::Angry) return RED;
    if (m == Mood::Worried) return AMBER;
    return accent;
}

void set_color_target(uint32_t rgb) {
    col_t[0] = (rgb >> 16) & 0xFF;
    col_t[1] = (rgb >> 8) & 0xFF;
    col_t[2] = rgb & 0xFF;
}

lv_color_t cur_color(float scale = 1) {
    return lv_color_make((uint8_t)(col[0] * scale), (uint8_t)(col[1] * scale),
                         (uint8_t)(col[2] * scale));
}

float frand(float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); }

float ease(float cur, float tgt, float k) { return cur + (tgt - cur) * k; }

void fill_rect(lv_layer_t *layer, float x1, float y1, float x2, float y2, lv_color_t c,
               int32_t radius, lv_opa_t opa = LV_OPA_COVER) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = opa;
    d.radius = radius;
    lv_area_t a = {(int32_t)lroundf(x1), (int32_t)lroundf(y1), (int32_t)lroundf(x2),
                   (int32_t)lroundf(y2)};
    lv_draw_rect(layer, &d, &a);
}

void fill_tri(lv_layer_t *layer, float x0, float y0, float x1, float y1, float x2, float y2,
              lv_color_t c) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.p[0] = {(lv_value_precise_t)x0, (lv_value_precise_t)y0};
    d.p[1] = {(lv_value_precise_t)x1, (lv_value_precise_t)y1};
    d.p[2] = {(lv_value_precise_t)x2, (lv_value_precise_t)y2};
    lv_draw_triangle(layer, &d);
}

void draw_text(lv_layer_t *layer, const char *txt, const lv_font_t *font, float x, float y,
               lv_color_t c, lv_opa_t opa) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = txt;
    d.font = font;
    d.color = c;
    d.opa = opa;
    lv_area_t a = {(int32_t)x, (int32_t)y, (int32_t)x + 60, (int32_t)y + 60};
    lv_draw_label(layer, &d, &a);
}

void draw_cb(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const float mid = a.x1 + lv_area_get_width(&a) / 2.0f;
    const lv_color_t bg = lv_color_black();
    const lv_color_t c = cur_color();
    float eye_x[2], eye_top[2], eye_w[2], eye_h[2];

    for (int i = 0; i < 2; i++) {
        const Eye &E = cur[i];
        const float side = i == 0 ? -1 : 1;
        // The eye on the side we look toward gets a little bigger.
        const float persp = 1 + 0.12f * look_x * side;
        const float w = E.w * persp * (1 - 0.25f * night_k);
        float h = E.h * persp * blink;
        if (h < 4) h = 4;
        const float shrink = 1 - 0.25f * night_k;
        const float cx = mid + side * (EYE_GAP / 2 + 29) * shrink + look_x * LOOK_X;
        const float cy = a.y1 + EYE_Y + E.dy + 56 * night_k + look_y * LOOK_Y;
        const float x1 = cx - w / 2, x2 = cx + w / 2, y1 = cy - h / 2, y2 = cy + h / 2;
        int32_t r = (int32_t)fminf(E.r, fminf(w, h) / 2);
        eye_x[i] = cx; eye_top[i] = y1; eye_w[i] = w; eye_h[i] = h;

        fill_rect(layer, x1, y1, x2, y2, c, r);

        if (E.lid_top > 0.005f || fabsf(E.tilt) > 0.5f) {
            // Lid edge through (cx, y1 + lid_top*h); the inner corner of the
            // left eye is on its right, so the slope flips per eye. A rect
            // covers down to the edge's highest point and one triangle the
            // sloped rest: two triangles sharing a diagonal leave an
            // anti-aliased seam.
            const float k = tanf(E.tilt * (float)M_PI / 180) * (i == 0 ? 1 : -1);
            const float yl = y1 + E.lid_top * h;
            const float xa = x1 - 7, xb = x2 + 7;
            const float ya = yl + k * (xa - cx), yb = yl + k * (xb - cx);
            const float ymin = floorf(fminf(ya, yb));
            fill_rect(layer, xa, y1 - 8, xb, ymin + 1, bg, 0);
            if (fabsf(ya - yb) > 0.5f) {
                if (ya > yb) fill_tri(layer, xa, ymin, xb, ymin, xa, ya, bg);
                else fill_tri(layer, xa, ymin, xb, ymin, xb, yb, bg);
            }
        }
        if (E.lid_bot > 0.005f)
            fill_rect(layer, x1 - 7, y2 - E.lid_bot * h, x2 + 7, y2 + 7, bg, 0);
        if (E.happy > 0.005f) {
            const float R = w * 0.9f;
            const float ccy = y2 - E.happy * h + R;
            fill_rect(layer, cx - R, ccy - R, cx + R, ccy + R, bg, LV_RADIUS_CIRCLE);
        }
    }

    if (mood == Mood::Asleep) {
        // Three z's drifting up and to the right.
        static const lv_font_t *fonts[3] = {&lv_font_montserrat_14, &lv_font_montserrat_20,
                                            &lv_font_montserrat_28};
        for (int i = 0; i < 3; i++) {
            float p = fmodf(anim_t / 3.0f + i / 3.0f, 1.0f);
            // Above the right eye; beside it at night (the clock is above).
            float x = night ? eye_x[1] + eye_w[1] / 2 + 6 + p * 22 : mid + 40 + p * 34;
            float y = night ? eye_top[1] - 6 - p * 28 : eye_top[1] - 34 - p * 40;
            lv_opa_t opa = (lv_opa_t)(255 * sinf(p * (float)M_PI));
            draw_text(layer, p < 0.5f ? "z" : "Z", fonts[i], x, y, cur_color(0.8f), opa);
        }
    }

    if (question) {
        float bob = sinf(anim_t * 2.2f) * 4;
        draw_text(layer, "?", &lv_font_montserrat_48, mid + 66, a.y1 + 26 + bob, c,
                  LV_OPA_COVER);
    }
}

}  // namespace

lv_obj_t *face_create(lv_obj_t *parent) {
    obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    shape_for(mood, tgt);
    shape_for(mood, cur);
    set_color_target(accent);
    for (int i = 0; i < 3; i++) col[i] = col_t[i];
    return obj;
}

void face_set_mood(Mood m) {
    if (m == mood) return;
    mood = m;
    shape_for(m, tgt);
    set_color_target(color_for(m));
}

Mood face_mood() { return mood; }

void face_set_color(lv_color_t c) {
    accent = lv_color_to_u32(c) & 0xFFFFFF;
    set_color_target(color_for(mood));
}

void face_look(float x, float y) {
    look_free = isnan(x) || isnan(y);
    if (!look_free) {
        look_tx = fmaxf(-1, fminf(1, x));
        look_ty = fmaxf(-1, fminf(1, y));
    }
}

void face_set_night(bool on) { night = on; }
void face_set_question(bool on) { question = on; }

void face_tick(uint32_t now) {
    float dt = last_ms ? (now - last_ms) / 1000.0f : 0;
    if (dt > 0.2f) dt = 0.2f;
    last_ms = now;
    anim_t += dt;

    // Blinking (not while asleep or startled).
    if (!blink_at) blink_at = now + 2000;
    if (mood != Mood::Asleep && !blink_start && now >= blink_at) blink_start = now;
    if (blink_start) {
        float t = (now - blink_start) / 170.0f;
        if (t >= 1) {
            blink = 1;
            blink_start = 0;
            // Sometimes a quick double blink.
            blink_at = now + (rand() % 5 == 0 ? 180 : (uint32_t)frand(2200, 6500));
        } else {
            blink = 1 - 0.92f * sinf(t * (float)M_PI);
        }
    }

    // Wandering gaze.
    if (look_free) {
        if (mood == Mood::Asleep) {
            look_tx = 0; look_ty = 0.2f;
        } else if (now >= wander_at) {
            if (rand() % 3 == 0) { look_tx = 0; look_ty = 0; }
            else { look_tx = frand(-0.7f, 0.7f); look_ty = frand(-0.45f, 0.45f); }
            wander_at = now + (uint32_t)frand(1200, 4200);
        }
    }

    const float k_look = 1 - expf(-dt / 0.07f);
    const float k_shape = 1 - expf(-dt / 0.11f);
    const float k_col = 1 - expf(-dt / 0.25f);
    look_x = ease(look_x, look_tx, k_look);
    look_y = ease(look_y, look_ty, k_look);
    for (int i = 0; i < 2; i++) {
        Eye &c = cur[i];
        const Eye &t = tgt[i];
        c.w = ease(c.w, t.w, k_shape);
        c.h = ease(c.h, t.h, k_shape);
        c.r = ease(c.r, t.r, k_shape);
        c.lid_top = ease(c.lid_top, t.lid_top, k_shape);
        c.tilt = ease(c.tilt, t.tilt, k_shape);
        c.lid_bot = ease(c.lid_bot, t.lid_bot, k_shape);
        c.happy = ease(c.happy, t.happy, k_shape);
        c.dy = ease(c.dy, t.dy, k_shape);
    }
    for (int i = 0; i < 3; i++) col[i] = ease(col[i], col_t[i], k_col);
    night_k = ease(night_k, night ? 1 : 0, k_shape * 0.5f);

    // Only the band with the eyes changes every frame; the curved text and
    // rim below stay untouched.
    if (obj) {
        lv_area_t a;
        lv_obj_get_coords(obj, &a);
        lv_area_t band = {a.x1, a.y1 + 20, a.x2, a.y1 + 214};
        lv_obj_invalidate_area(obj, &band);
    }
}
