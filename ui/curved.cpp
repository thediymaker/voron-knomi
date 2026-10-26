#include "curved.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct Curved {
    int max;
    lv_obj_t **letters;
    char text[160];
    const lv_font_t *font;
    uint32_t color;
    float radius;
    bool bottom;
};

// Decodes one UTF-8 character at txt[*i] and advances *i.
uint32_t utf8_next(const char *txt, uint32_t *i) {
    const uint8_t c = txt[*i];
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = extra ? c & (0x3F >> extra) : c;
    (*i)++;
    while (extra-- > 0 && (txt[*i] & 0xC0) == 0x80) cp = cp << 6 | (txt[(*i)++] & 0x3F);
    return cp;
}

}  // namespace

lv_obj_t *curved_create(lv_obj_t *parent, int max_chars) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 240, 240);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    Curved *cv = (Curved *)calloc(1, sizeof(Curved));
    cv->max = max_chars;
    cv->letters = (lv_obj_t **)calloc(max_chars, sizeof(lv_obj_t *));
    for (int i = 0; i < max_chars; i++) {
        lv_obj_t *l = lv_label_create(c);
        lv_label_set_text(l, "");
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        cv->letters[i] = l;
    }
    lv_obj_set_user_data(c, cv);
    return c;
}

void curved_set_text(lv_obj_t *c, const char *txt, const lv_font_t *font, uint32_t color,
                     float radius, bool bottom) {
    Curved *cv = (Curved *)lv_obj_get_user_data(c);
    if (!txt) txt = "";
    if (!strncmp(cv->text, txt, sizeof cv->text - 1) && cv->font == font && cv->color == color &&
        cv->radius == radius && cv->bottom == bottom)
        return;
    strncpy(cv->text, txt, sizeof cv->text - 1);
    cv->font = font;
    cv->color = color;
    cv->radius = radius;
    cv->bottom = bottom;

    // Split into UTF-8 characters and measure them.
    struct Ch { char s[5]; int32_t w; };
    Ch chars[96];
    int n = 0;
    float total = 0;
    const int32_t space = 1;
    for (uint32_t i = 0; txt[i] && n < cv->max && n < 96;) {
        const uint32_t start = i;
        const uint32_t cp = utf8_next(txt, &i);
        uint32_t peek = i;
        const uint32_t next = txt[i] ? utf8_next(txt, &peek) : 0;
        Ch &ch = chars[n++];
        const uint32_t len = i - start < 4 ? i - start : 4;
        memcpy(ch.s, txt + start, len);
        ch.s[len] = 0;
        ch.w = lv_font_get_glyph_width(font, cp, next) + space;
        total += ch.w;
    }
    const int32_t lh = lv_font_get_line_height(font);
    const float span = total / radius;  // radians
    // Bottom: angles run from left (larger) to right (smaller) around 90 deg.
    // Top: from left (smaller) to right (larger) around 270 deg.
    const float centre = bottom ? (float)M_PI / 2 : 3 * (float)M_PI / 2;
    float acc = 0;
    for (int k = 0; k < cv->max; k++) {
        lv_obj_t *l = cv->letters[k];
        if (k >= n) {
            lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const Ch &ch = chars[k];
        const float along = acc + ch.w / 2.0f;
        acc += ch.w;
        const float th = bottom ? centre + span / 2 - along / radius : centre - span / 2 + along / radius;
        const float x = 120 + radius * cosf(th), y = 120 + radius * sinf(th);
        const float rot = (th * 180 / (float)M_PI) + (bottom ? -90 : 90);
        lv_label_set_text(l, ch.s);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
        lv_obj_set_size(l, ch.w, lh);
        lv_obj_set_pos(l, (int32_t)lroundf(x - ch.w / 2.0f), (int32_t)lroundf(y - lh / 2.0f));
        lv_obj_set_style_transform_pivot_x(l, ch.w / 2, 0);
        lv_obj_set_style_transform_pivot_y(l, lh / 2, 0);
        lv_obj_set_style_transform_rotation(l, (int32_t)lroundf(rot * 10), 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
}
