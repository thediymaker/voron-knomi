// Serial log plus a RAM ring buffer the web UI can show (no USB needed).
#pragma once
#include <Arduino.h>

void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
String log_dump();
