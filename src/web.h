// Web UI on port 80. Everything needs the admin password (HTTP digest auth);
// until one is set, the only page is the "set a password" form.
#pragma once
#include "state.h"

void web_begin(PrinterState &s);
void web_loop();
