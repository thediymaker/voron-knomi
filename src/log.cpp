#include "log.h"
#include <stdarg.h>

static char ring[8192];
static size_t head;  // next write position
static bool wrapped;

void logf(const char *fmt, ...) {
    char line[256];
    int n = snprintf(line, sizeof line, "[%7.1f] ", millis() / 1000.0);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
    va_end(ap);
    strcat(line, "\n");
    Serial.print(line);
    for (const char *p = line; *p; p++) {
        ring[head++] = *p;
        if (head == sizeof ring) {
            head = 0;
            wrapped = true;
        }
    }
}

String log_dump() {
    String out;
    out.reserve(sizeof ring);
    if (wrapped)
        for (size_t i = head; i < sizeof ring; i++) out += ring[i];
    for (size_t i = 0; i < head; i++) out += ring[i];
    return out;
}
