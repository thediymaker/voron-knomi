// Colour themes: one accent for the main data (progress, nozzle, active
// lane) and a second colour for secondary data (bed). Errors stay red.
#pragma once
#include <stdint.h>

struct ThemePreset {
    const char *id;
    const char *name;
    uint32_t accent;
    uint32_t second;
};

extern const ThemePreset THEMES[];
extern const int THEME_COUNT;
// Falls back to the first theme for unknown ids.
const ThemePreset &theme_find(const char *id);

// Fixed colours shared by every theme.
static const uint32_t COL_TEXT = 0xFFFFFF, COL_MUTED = 0x8E8E93, COL_DIM = 0x3A3A3C,
                      COL_TRACK = 0x232629, COL_RED = 0xFF453A, COL_AMBER = 0xFFB020,
                      COL_WHITE_EYES = 0xF2F5F7;
