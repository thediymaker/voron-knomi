// Wi-Fi: joins the configured network; when that fails it also opens a setup
// access point whose random password is shown only on the KNOMI's screen.
#pragma once
#include "state.h"

void net_begin(PrinterState &s);
void net_loop(uint32_t now, PrinterState &s);
// Reconnect with the current cfg (after the web UI changes Wi-Fi settings).
void net_reconnect();
