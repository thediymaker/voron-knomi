#include "theme.h"
#include <string.h>

const ThemePreset THEMES[] = {
    {"neon", "Neon", 0xC6FF1A, 0x00D1FF},
    {"cyan", "Cyan", 0x38D9F5, 0xFF9F0A},
    {"amber", "Amber", 0xFFB020, 0xFF5E3A},
    {"magenta", "Magenta", 0xFF4FD8, 0x7B8CFF},
    {"ice", "Ice", 0xE8F1FF, 0x8AB4FF},
};
const int THEME_COUNT = sizeof THEMES / sizeof THEMES[0];

const ThemePreset &theme_find(const char *id) {
    for (int i = 0; i < THEME_COUNT; i++)
        if (id && strcmp(THEMES[i].id, id) == 0) return THEMES[i];
    return THEMES[0];
}
