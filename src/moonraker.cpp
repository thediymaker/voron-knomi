#include "moonraker.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include "config.h"
#include "log.h"
#include "version.h"

namespace {

enum Req { R_IDENTIFY = 1, R_INFO, R_SUB_BASE, R_SUB_LANES, R_META, R_GCODE };

WebSocketsClient ws;
PrinterState *S;
uint32_t seq, info_poll_at;
bool started;

// Thumbnail hand-over between the download task and the main loop.
QueueHandle_t thumb_q;
portMUX_TYPE thumb_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t *thumb_png;
size_t thumb_len;
bool thumb_ready;

void send(const char *method, JsonDocument *params, Req type) {
    JsonDocument d;
    d["jsonrpc"] = "2.0";
    d["method"] = method;
    if (params) d["params"] = *params;
    d["id"] = (int)type * 100000 + (int)(seq++ % 100000);
    String out;
    serializeJson(d, out);
    ws.sendTXT(out);
}

void add_fields(JsonObject objs, const String &name, std::initializer_list<const char *> fields) {
    JsonArray a = objs[name].to<JsonArray>();
    for (const char *f : fields) a.add(f);
}

// Two steps: the base objects (which include AFC's lane list), then again
// with one object per lane. Unknown objects are ignored by Moonraker.
void subscribe(bool lanes) {
    JsonDocument p;
    JsonObject o = p["objects"].to<JsonObject>();
    add_fields(o, "print_stats", {"state", "filename", "print_duration", "filament_used", "info"});
    add_fields(o, "display_status", {"progress", "message"});
    add_fields(o, "extruder", {"temperature", "target"});
    add_fields(o, "heater_bed", {"temperature", "target"});
    add_fields(o, "toolhead", {"homed_axes", "axis_maximum"});
    add_fields(o, "motion_report", {"live_position", "live_velocity"});
    add_fields(o, "idle_timeout", {"state"});
    add_fields(o, "webhooks", {"state", "state_message"});
    add_fields(o, "AFC", {"current_load", "current_lane", "current_state", "error_state",
                          "message", "current_toolchange", "number_of_toolchanges", "lanes",
                          "led_state"});
    if (lanes) {
        for (int i = 0; i < S->lane_count; i++) {
            for (const char *kind : {"AFC_stepper ", "AFC_lane "}) {
                add_fields(o, String(kind) + S->lanes[i].name,
                           {"map", "material", "status", "color", "weight", "initial_weight",
                            "prep", "load", "tool_loaded"});
            }
        }
    }
    send("printer.objects.subscribe", &p, lanes ? R_SUB_LANES : R_SUB_BASE);
}

void copy(char *dst, size_t n, JsonVariantConst v) {
    const char *s = v.is<const char *>() ? v.as<const char *>() : "";
    strlcpy(dst, s, n);
}

Job job_from(const char *s) {
    if (!strcmp(s, "printing")) return Job::Printing;
    if (!strcmp(s, "paused")) return Job::Paused;
    if (!strcmp(s, "complete")) return Job::Complete;
    if (!strcmp(s, "cancelled")) return Job::Cancelled;
    if (!strcmp(s, "error")) return Job::Error;
    return Job::Standby;
}

Klippy klippy_from(const char *s) {
    if (!strcmp(s, "ready")) return Klippy::Ready;
    if (!strcmp(s, "startup")) return Klippy::Startup;
    if (!strcmp(s, "shutdown")) return Klippy::Shutdown;
    if (!strcmp(s, "error")) return Klippy::Error;
    return Klippy::Offline;
}

uint32_t parse_color(const char *s) {
    if (*s == '#') s++;
    return strtoul(s, nullptr, 16) & 0xFFFFFF;
}

void request_metadata() {
    if (!S->filename[0]) return;
    JsonDocument p;
    p["filename"] = S->filename;
    send("server.files.metadata", &p, R_META);
}

void queue_thumbnail(const char *rel) {
    // Thumbnail paths are relative to the gcode file's folder.
    String path = S->filename;
    int slash = path.lastIndexOf('/');
    path = slash >= 0 ? path.substring(0, slash + 1) + rel : String(rel);
    String url = String("http://") + cfg.mr_host + ":" + cfg.mr_port + "/server/files/gcodes/";
    for (char c : path) {
        if (isalnum((unsigned char)c) || strchr("-_.~/", c)) url += c;
        else {
            char hex[4];
            snprintf(hex, sizeof hex, "%%%02X", (uint8_t)c);
            url += hex;
        }
    }
    char *u = strdup(url.c_str());
    if (xQueueSend(thumb_q, &u, 0) != pdTRUE) free(u);
}

void publish_thumbnail(uint8_t *png, size_t len) {
    portENTER_CRITICAL(&thumb_mux);
    uint8_t *old = thumb_ready ? thumb_png : nullptr;
    thumb_png = png;
    thumb_len = len;
    thumb_ready = true;
    portEXIT_CRITICAL(&thumb_mux);
    free(old);
}

void merge_lane(const char *name, JsonObjectConst o) {
    const int i = lane_index(*S, name);
    if (i < 0) return;
    Lane &L = S->lanes[i];
    if (o["map"].is<const char *>()) copy(L.map, sizeof L.map, o["map"]);
    if (o["material"].is<const char *>()) copy(L.material, sizeof L.material, o["material"]);
    if (o["status"].is<const char *>()) copy(L.status, sizeof L.status, o["status"]);
    if (o["color"].is<const char *>()) {
        const char *c = o["color"];
        L.has_color = c[0] != 0;
        if (L.has_color) L.color = parse_color(c);
    }
    if (o["weight"].is<float>()) L.weight = o["weight"];
    if (o["initial_weight"].is<float>()) L.initial_weight = o["initial_weight"];
    if (o["prep"].is<bool>()) L.prep = o["prep"];
    if (o["load"].is<bool>()) L.load = o["load"];
    if (o["tool_loaded"].is<bool>()) L.tool_loaded = o["tool_loaded"];
}

// Returns true when the AFC lane list changed (needs a re-subscribe).
bool merge(JsonObjectConst st) {
    bool lanes_changed = false;
    JsonObjectConst o;
    if (!(o = st["print_stats"]).isNull()) {
        if (o["state"].is<const char *>()) S->job = job_from(o["state"]);
        if (o["print_duration"].is<float>()) S->print_duration = o["print_duration"];
        if (o["filament_used"].is<float>()) S->filament_used = o["filament_used"];
        if (o["filename"].is<const char *>() && strcmp(S->filename, o["filename"]) != 0) {
            copy(S->filename, sizeof S->filename, o["filename"]);
            S->slicer_estimate = 0;
            publish_thumbnail(nullptr, 0);
            request_metadata();
        }
        JsonObjectConst info = o["info"];
        if (!info.isNull()) {
            S->layer = info["current_layer"].is<int>() ? info["current_layer"].as<int>() : -1;
            S->layers = info["total_layer"].is<int>() ? info["total_layer"].as<int>() : -1;
        }
    }
    if (!(o = st["display_status"]).isNull()) {
        if (o["progress"].is<float>()) S->progress = o["progress"];
        if (!o["message"].isUnbound()) {
            const char *m = o["message"] | "";
            if (strcmp(m, S->display_msg) != 0) {
                strlcpy(S->display_msg, m, sizeof S->display_msg);
                S->display_msg_ms = millis();
            }
        }
    }
    if (!(o = st["extruder"]).isNull()) {
        if (o["temperature"].is<float>()) S->nozzle = o["temperature"];
        if (o["target"].is<float>()) S->nozzle_target = o["target"];
    }
    if (!(o = st["heater_bed"]).isNull()) {
        if (o["temperature"].is<float>()) S->bed = o["temperature"];
        if (o["target"].is<float>()) S->bed_target = o["target"];
    }
    if (!(o = st["toolhead"]).isNull()) {
        if (o["homed_axes"].is<const char *>()) copy(S->homed, sizeof S->homed, o["homed_axes"]);
        JsonArrayConst m = o["axis_maximum"];
        if (m.size() >= 2) {
            S->axis_max[0] = fmaxf(1, m[0].as<float>());
            S->axis_max[1] = fmaxf(1, m[1].as<float>());
        }
    }
    if (!(o = st["motion_report"]).isNull()) {
        JsonArrayConst p = o["live_position"];
        if (p.size() >= 3)
            for (int i = 0; i < 3; i++) S->pos[i] = p[i];
        if (o["live_velocity"].is<float>()) S->velocity = o["live_velocity"];
    }
    if (!(o = st["idle_timeout"]).isNull() && o["state"].is<const char *>())
        S->busy = !strcmp(o["state"], "Printing");
    if (!(o = st["webhooks"]).isNull()) {
        if (o["state"].is<const char *>()) S->klippy = klippy_from(o["state"]);
        if (o["state_message"].is<const char *>())
            copy(S->klippy_msg, sizeof S->klippy_msg, o["state_message"]);
    }
    if (!(o = st["AFC"]).isNull()) {
        S->afc = true;
        if (!o["current_load"].isUnbound()) copy(S->afc_lane, sizeof S->afc_lane, o["current_load"]);
        if (!o["current_lane"].isUnbound()) copy(S->afc_moving, sizeof S->afc_moving, o["current_lane"]);
        if (o["current_state"].is<const char *>()) copy(S->afc_state, sizeof S->afc_state, o["current_state"]);
        if (o["error_state"].is<bool>()) S->afc_error = o["error_state"];
        if (o["message"]["message"].is<const char *>()) copy(S->afc_msg, sizeof S->afc_msg, o["message"]["message"]);
        if (o["current_toolchange"].is<int>()) S->toolchange = o["current_toolchange"];
        if (o["number_of_toolchanges"].is<int>()) S->toolchanges = o["number_of_toolchanges"];
        if (o["led_state"].is<bool>()) S->afc_led = o["led_state"];
        JsonArrayConst lanes = o["lanes"];
        if (!lanes.isNull()) {
            int n = 0;
            for (JsonVariantConst v : lanes) {
                if (n == MAX_LANES) break;
                const char *name = v.as<const char *>();
                if (!name) continue;
                if (n >= S->lane_count || strcmp(S->lanes[n].name, name) != 0) {
                    S->lanes[n] = Lane();
                    strlcpy(S->lanes[n].name, name, sizeof S->lanes[n].name);
                    S->lanes[n].weight = -1;
                    lanes_changed = true;
                }
                n++;
            }
            if (n != S->lane_count) lanes_changed = true;
            S->lane_count = n;
        }
    }
    for (JsonPairConst kv : st) {
        const char *k = kv.key().c_str();
        if (!strncmp(k, "AFC_stepper ", 12)) merge_lane(k + 12, kv.value());
        else if (!strncmp(k, "AFC_lane ", 9)) merge_lane(k + 9, kv.value());
    }
    return lanes_changed;
}

void on_gcode_response(const char *line) {
    if (!line) return;
    if (strstr(line, "probe at ") || strstr(line, "Retries:") || strstr(line, "Probed points"))
        S->probe_ms = millis();
    if (!strncmp(line, "!! ", 3)) S->gcode_error_ms = millis();
}

void on_klippy(Klippy k) {
    S->klippy = k;
    if (k == Klippy::Ready) subscribe(false);
    else info_poll_at = millis() + 2000;
}

void on_message(const uint8_t *payload, size_t len) {
    JsonDocument d;
    if (deserializeJson(d, payload, len)) return;
    const char *method = d["method"];
    if (method) {
        if (!strcmp(method, "notify_status_update")) {
            if (merge(d["params"][0])) subscribe(true);
        } else if (!strcmp(method, "notify_gcode_response")) {
            on_gcode_response(d["params"][0]);
        } else if (!strcmp(method, "notify_klippy_ready")) {
            on_klippy(Klippy::Ready);
        } else if (!strcmp(method, "notify_klippy_shutdown")) {
            on_klippy(Klippy::Shutdown);
        } else if (!strcmp(method, "notify_klippy_disconnected")) {
            on_klippy(Klippy::Offline);
        }
        return;
    }
    if (!d["id"].is<long>()) return;
    const int type = d["id"].as<long>() / 100000;
    if (!d["error"].isNull()) {
        logf("moonraker: request %d failed: %s", type, d["error"]["message"].as<const char *>());
        return;
    }
    JsonObjectConst r = d["result"];
    switch (type) {
    case R_INFO: {
        const char *ks = r["klippy_state"] | "disconnected";
        logf("moonraker: klippy %s", ks);
        on_klippy(klippy_from(ks));
        break;
    }
    case R_SUB_BASE:
    case R_SUB_LANES:
        if (merge(r["status"]) || (type == R_SUB_BASE && S->lane_count > 0)) {
            logf("moonraker: subscribed, %d AFC lanes", S->lane_count);
            subscribe(true);
        }
        break;
    case R_META: {
        S->slicer_estimate = r["estimated_time"] | 0.0f;
        // Pick the largest thumbnail up to 400 px wide.
        const char *best = nullptr;
        int best_w = 0;
        for (JsonObjectConst t : r["thumbnails"].as<JsonArrayConst>()) {
            const int w = t["width"] | 0;
            if (w > best_w && w <= 400 && t["relative_path"].is<const char *>()) {
                best_w = w;
                best = t["relative_path"];
            }
        }
        if (best) queue_thumbnail(best);
        break;
    }
    default: break;
    }
}

void on_event(WStype_t type, uint8_t *payload, size_t len) {
    switch (type) {
    case WStype_CONNECTED: {
        logf("moonraker: connected to %s:%u", cfg.mr_host, cfg.mr_port);
        S->moonraker = true;
        JsonDocument p;
        p["client_name"] = "KNOMI-NG";
        p["version"] = FW_VERSION;
        p["type"] = "display";
        p["url"] = "https://github.com/bigtreetech/KNOMI";
        if (cfg.mr_key[0]) p["api_key"] = cfg.mr_key;
        send("server.connection.identify", &p, R_IDENTIFY);
        send("server.info", nullptr, R_INFO);
        break;
    }
    case WStype_DISCONNECTED:
        if (S->moonraker) logf("moonraker: disconnected");
        S->moonraker = false;
        S->klippy = Klippy::Offline;
        break;
    case WStype_TEXT: on_message(payload, len); break;
    default: break;
    }
}

void thumb_task(void *) {
    for (;;) {
        char *url;
        xQueueReceive(thumb_q, &url, portMAX_DELAY);
        HTTPClient http;
        http.setTimeout(8000);
        http.begin(url);
        if (cfg.mr_key[0]) http.addHeader("X-Api-Key", cfg.mr_key);
        const int code = http.GET();
        const int size = http.getSize();
        if (code == 200 && size > 0 && size < 512 * 1024) {
            uint8_t *buf = (uint8_t *)ps_malloc(size);
            WiFiClient *st = http.getStreamPtr();
            int got = 0;
            const uint32_t deadline = millis() + 10000;
            while (buf && got < size && millis() < deadline) {
                const int n = st->readBytes(buf + got, size - got);
                if (n <= 0) delay(5);
                got += n > 0 ? n : 0;
            }
            if (buf && got == size) publish_thumbnail(buf, size);
            else free(buf);
            logf("moonraker: thumbnail %d bytes", got);
        } else {
            logf("moonraker: thumbnail HTTP %d", code);
        }
        http.end();
        free(url);
    }
}

}  // namespace

void mr_begin(PrinterState &s) {
    S = &s;
    thumb_q = xQueueCreate(2, sizeof(char *));
    xTaskCreatePinnedToCore(thumb_task, "thumb", 8192, nullptr, 1, nullptr, 0);
    ws.onEvent(on_event);
    ws.setReconnectInterval(3000);
    ws.enableHeartbeat(15000, 5000, 2);
    mr_reconnect();
}

void mr_reconnect() {
    if (started) ws.disconnect();
    started = false;
    if (!cfg.mr_host[0]) return;
    // The library sends "Origin: file://" by default; Moonraker rejects that
    // origin (not in cors_domains) with a 403. Without an Origin header it is
    // a plain non-browser client.
    ws.setExtraHeaders("");
    ws.begin(cfg.mr_host, cfg.mr_port, "/websocket", "");
    started = true;
}

void mr_loop(uint32_t now) {
    if (!started) return;
    ws.loop();
    if (S->moonraker && S->klippy != Klippy::Ready && info_poll_at && now >= info_poll_at) {
        info_poll_at = 0;
        send("server.info", nullptr, R_INFO);
    }
}

void mr_gcode(const char *script) {
    if (!S->moonraker) return;
    logf("moonraker: gcode %s", script);
    JsonDocument p;
    p["script"] = script;
    send("printer.gcode.script", &p, R_GCODE);
}

bool mr_take_thumbnail(uint8_t **png, size_t *len) {
    portENTER_CRITICAL(&thumb_mux);
    const bool ready = thumb_ready;
    if (ready) {
        *png = thumb_png;
        *len = thumb_len;
        thumb_png = nullptr;
        thumb_ready = false;
    }
    portEXIT_CRITICAL(&thumb_mux);
    return ready;
}
