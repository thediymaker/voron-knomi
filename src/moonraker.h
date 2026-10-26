// Moonraker websocket client: subscribes to printer objects and keeps the
// PrinterState current; fetches the job's thumbnail in the background.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "state.h"

void mr_begin(PrinterState &s);
void mr_loop(uint32_t now);
// Reconnect with the current cfg (after the web UI changes it).
void mr_reconnect();
// Runs a G-code script on the printer (fire and forget; errors are logged).
void mr_gcode(const char *script);
// When a new thumbnail (or "no thumbnail": png == nullptr) is ready, hands
// over the heap buffer and returns true. The caller frees it with free().
bool mr_take_thumbnail(uint8_t **png, size_t *len);
