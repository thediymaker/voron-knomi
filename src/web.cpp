#include "web.h"
#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include "config.h"
#include "log.h"
#include "moonraker.h"
#include "net.h"
#include "theme.h"
#include "ui.h"
#include "version.h"
#include "web_pages.h"

void apply_ui_options();  // main.cpp
void apply_timezone();    // main.cpp

namespace {

WebServer server(80);
PrinterState *S;
bool ota_authed, ota_failed;
uint32_t restart_at;

// Browser logins get a random session token in a cookie (kept in RAM, so a
// reboot logs everyone out). Scripts can send HTTP auth for user "admin"
// instead. No WWW-Authenticate challenge is ever sent: browsers would show
// their own prompt.
const int MAX_SESSIONS = 6;
char sessions[MAX_SESSIONS][33];
int next_session;

bool setup_mode() { return cfg.admin_pass[0] == 0; }

bool same(const String &a, const char *b) {
    const size_t n = strlen(b);
    if (a.length() != n) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

String cookie_token() {
    const String c = server.header("Cookie");
    const int i = c.indexOf("knomi_session=");
    if (i < 0) return "";
    const int start = i + 14;
    int end = c.indexOf(';', start);
    if (end < 0) end = c.length();
    return c.substring(start, end);
}

bool authed() {
    if (setup_mode()) {
        server.send(401, "application/json", "{\"error\":\"set a password first\"}");
        return false;
    }
    const String t = cookie_token();
    if (t.length() == 32)
        for (auto &s : sessions)
            if (s[0] && same(t, s)) return true;
    if (server.hasHeader("Authorization") && server.authenticate("admin", cfg.admin_pass)) return true;
    server.send(401, "application/json", "{\"error\":\"login required\"}");
    return false;
}

void start_session() {
    char *s = sessions[next_session++ % MAX_SESSIONS];
    for (int i = 0; i < 32; i += 8) snprintf(s + i, 9, "%08x", (unsigned)esp_random());
    server.sendHeader("Set-Cookie", String("knomi_session=") + s +
                                        "; Path=/; HttpOnly; SameSite=Strict; Max-Age=2592000");
}

void send_json(JsonDocument &d, int code = 200) {
    String out;
    serializeJson(d, out);
    server.send(code, "application/json", out);
}

void send_error(int code, const char *msg) {
    JsonDocument d;
    d["error"] = msg;
    send_json(d, code);
}

const esp_partition_t *other_app() { return esp_ota_get_next_update_partition(nullptr); }

String app_name(const esp_partition_t *p) {
    esp_app_desc_t d;
    if (!p || esp_ota_get_partition_description(p, &d) != ESP_OK) return "";
    return String(d.project_name) + " " + d.version;
}

void handle_root() {
    if (setup_mode()) {
        server.send_P(200, "text/html", SETUP_HTML);
        return;
    }
    const String t = cookie_token();
    bool ok = false;
    if (t.length() == 32)
        for (auto &s : sessions) ok |= s[0] && same(t, s);
    server.send_P(200, "text/html", ok ? INDEX_HTML : LOGIN_HTML);
}

void handle_login() {
    if (setup_mode()) return send_error(409, "set a password first");
    if (!same(server.arg("password"), cfg.admin_pass)) {
        delay(1500);  // slow down guessing
        logf("web: failed login from %s", server.client().remoteIP().toString().c_str());
        return send_error(403, "wrong password");
    }
    start_session();
    server.send(200, "text/plain", "ok");
}

void handle_logout() {
    const String t = cookie_token();
    for (auto &s : sessions)
        if (s[0] && same(t, s)) s[0] = 0;
    server.sendHeader("Set-Cookie", "knomi_session=; Path=/; Max-Age=0");
    server.send(200, "text/plain", "ok");
}

void handle_setup() {
    if (!setup_mode()) return send_error(409, "password already set");
    const String pw = server.arg("password");
    if (pw.length() < 8 || pw.length() >= sizeof cfg.admin_pass)
        return send_error(400, "use 8 to 64 characters");
    strlcpy(cfg.admin_pass, pw.c_str(), sizeof cfg.admin_pass);
    config_save();
    logf("web: admin password set");
    start_session();
    server.send(200, "text/plain", "ok");
}

void handle_status() {
    if (!authed()) return;
    JsonDocument d;
    d["version"] = FW_VERSION;
    d["uptime_s"] = millis() / 1000;
    d["ip"] = S->ip;
    d["rssi"] = WiFi.RSSI();
    d["heap"] = ESP.getFreeHeap();
    d["psram"] = ESP.getFreePsram();
    d["moonraker"] = S->moonraker;
    static const char *kl[] = {"offline", "startup", "ready", "shutdown", "error"};
    d["klippy"] = kl[(int)S->klippy];
    d["afc_lanes"] = S->lane_count;
    JsonArray mo = d["motion_g"].to<JsonArray>();
    mo.add(roundf(S->motion[0] * 1000) / 1000);
    mo.add(roundf(S->motion[1] * 1000) / 1000);
    if (S->has_air) {
        d["air_temp"] = S->air_temp;
        d["air_hum"] = S->air_hum;
    }
    const esp_partition_t *run = esp_ota_get_running_partition();
    d["running"] = String(run->label) + ": " + app_name(run);
    const esp_partition_t *o = other_app();
    d["other"] = o ? String(o->label) + ": " + app_name(o) : String("-");
    send_json(d);
}

void handle_get_config() {
    if (!authed()) return;
    JsonDocument d;
    d["wifi_ssid"] = cfg.wifi_ssid;
    d["wifi_pass_set"] = cfg.wifi_pass[0] != 0;
    d["hostname"] = cfg.hostname;
    d["mr_host"] = cfg.mr_host;
    d["mr_port"] = cfg.mr_port;
    d["mr_key_set"] = cfg.mr_key[0] != 0;
    d["theme"] = cfg.theme;
    JsonArray themes = d["themes"].to<JsonArray>();
    for (int i = 0; i < THEME_COUNT; i++) {
        JsonObject t = themes.add<JsonObject>();
        t["id"] = THEMES[i].id;
        t["name"] = THEMES[i].name;
    }
    char eyes[8];
    snprintf(eyes, sizeof eyes, "#%06X", (unsigned)(cfg.eyes & 0xFFFFFF));
    d["eyes"] = cfg.eyes == EYES_ACCENT ? "theme" : cfg.eyes == 0xF2F5F7 ? "white" : "custom";
    d["eyes_color"] = cfg.eyes == EYES_ACCENT ? "#F2F5F7" : eyes;
    d["gaze"] = cfg.gaze;
    d["tz"] = cfg.tz;
    d["clock24"] = cfg.clock24;
    d["afc_screen"] = cfg.afc_screen;
    d["inv_x"] = cfg.track_invert_x;
    d["inv_y"] = cfg.track_invert_y;
    d["sleep_s"] = cfg.sleep_s;
    d["brightness"] = cfg.brightness;
    d["sleep_brightness"] = cfg.sleep_brightness;
    send_json(d);
}

// Secrets: an empty field keeps the stored value.
void handle_post_config() {
    if (!authed()) return;
    bool wifi = false, mr = false;
    auto str = [](const char *arg, char *dst, size_t n, bool *changed) {
        if (!server.hasArg(arg)) return;
        String v = server.arg(arg);
        v.trim();
        if (strcmp(dst, v.c_str()) != 0) {
            strlcpy(dst, v.c_str(), n);
            if (changed) *changed = true;
        }
    };
    auto secret = [](const char *arg, char *dst, size_t n, bool *changed) {
        const String v = server.arg(arg);
        if (v.length() == 0) return;
        strlcpy(dst, v.c_str(), n);
        *changed = true;
    };
    str("wifi_ssid", cfg.wifi_ssid, sizeof cfg.wifi_ssid, &wifi);
    secret("wifi_pass", cfg.wifi_pass, sizeof cfg.wifi_pass, &wifi);
    str("hostname", cfg.hostname, sizeof cfg.hostname, &wifi);
    str("mr_host", cfg.mr_host, sizeof cfg.mr_host, &mr);
    if (server.hasArg("mr_port")) {
        const int port = server.arg("mr_port").toInt();
        if (port > 0 && port < 65536 && port != cfg.mr_port) {
            cfg.mr_port = port;
            mr = true;
        }
    }
    secret("mr_key", cfg.mr_key, sizeof cfg.mr_key, &mr);
    if (server.arg("mr_key_clear") == "1" && cfg.mr_key[0]) {
        cfg.mr_key[0] = 0;
        mr = true;
    }
    bool admin = false;
    const String np = server.arg("admin_pass");
    if (np.length() > 0) {
        if (np.length() < 8) return send_error(400, "admin password: use at least 8 characters");
        secret("admin_pass", cfg.admin_pass, sizeof cfg.admin_pass, &admin);
    }
    if (server.hasArg("theme")) strlcpy(cfg.theme, theme_find(server.arg("theme").c_str()).id, sizeof cfg.theme);
    if (server.hasArg("eyes")) {
        const String e = server.arg("eyes");
        if (e == "theme") cfg.eyes = EYES_ACCENT;
        else if (e == "white") cfg.eyes = 0xF2F5F7;
        else {
            String c = server.arg("eyes_color");
            if (c.startsWith("#")) c = c.substring(1);
            cfg.eyes = strtoul(c.c_str(), nullptr, 16) & 0xFFFFFF;
        }
    }
    if (server.hasArg("gaze")) cfg.gaze = constrain(server.arg("gaze").toInt(), 0, 2);
    bool tz = false;
    str("tz", cfg.tz, sizeof cfg.tz, &tz);
    cfg.clock24 = server.arg("clock24") == "1";
    cfg.afc_screen = server.arg("afc_screen") == "1";
    cfg.track_invert_x = server.arg("inv_x") == "1";
    cfg.track_invert_y = server.arg("inv_y") == "1";
    if (server.hasArg("sleep_s")) cfg.sleep_s = constrain(server.arg("sleep_s").toInt(), 30, 36000);
    if (server.hasArg("brightness")) cfg.brightness = constrain(server.arg("brightness").toInt(), 5, 100);
    if (server.hasArg("sleep_brightness"))
        cfg.sleep_brightness = constrain(server.arg("sleep_brightness").toInt(), 0, 100);
    config_save();
    apply_ui_options();
    if (tz) apply_timezone();
    logf("web: settings saved%s%s%s", wifi ? " (wifi)" : "", mr ? " (moonraker)" : "",
         admin ? " (admin password)" : "");
    server.send(200, "text/plain", wifi ? "saved, reconnecting Wi-Fi" : "saved");
    if (mr) mr_reconnect();
    if (wifi) net_reconnect();
}

void handle_scan() {
    if (!authed()) return;
    const int n = WiFi.scanNetworks();
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    for (int i = 0; i < n && i < 20; i++) {
        JsonObject o = a.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
    }
    WiFi.scanDelete();
    send_json(d);
}

void handle_log() {
    if (!authed()) return;
    server.send(200, "text/plain", log_dump());
}

// Streams a flash partition: "app" = the other OTA slot (the stock firmware
// after the first install), "fs" = the stock GIF filesystem, "nvs" = settings.
void handle_backup() {
    if (!authed()) return;
    const String part = server.arg("part");
    const esp_partition_t *p = nullptr;
    if (part == "app") p = other_app();
    else if (part == "fs") p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    else if (part == "nvs") p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    if (!p) return send_error(404, "unknown partition");
    server.setContentLength(p->size);
    server.sendHeader("Content-Disposition", String("attachment; filename=knomi-") + p->label + ".bin");
    server.send(200, "application/octet-stream", "");
    static uint8_t buf[4096];
    for (size_t off = 0; off < p->size; off += sizeof buf) {
        const size_t n = min(sizeof buf, (size_t)(p->size - off));
        if (esp_partition_read(p, off, buf, n) != ESP_OK) break;
        if (server.client().write(buf, n) != n) break;
    }
}

void handle_boot_other() {
    if (!authed()) return;
    const esp_partition_t *o = other_app();
    if (app_name(o).isEmpty()) return send_error(404, "no valid firmware in the other slot");
    if (esp_ota_set_boot_partition(o) != ESP_OK) return send_error(500, "could not switch");
    logf("web: booting %s", app_name(o).c_str());
    server.send(200, "text/plain", "rebooting into " + app_name(o));
    restart_at = millis() + 800;
}

void handle_restart() {
    if (!authed()) return;
    server.send(200, "text/plain", "restarting");
    restart_at = millis() + 500;
}

void handle_tile() {
    if (!authed()) return;
    ui_show_tile(server.arg("i").toInt());
    server.send(200, "text/plain", "ok");
}

// The current screen as a 24-bit BMP.
void handle_screenshot() {
    if (!authed()) return;
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!snap) return send_error(500, "no memory");
    const uint32_t w = snap->header.w, h = snap->header.h, row = (w * 3 + 3) & ~3u;
    const uint32_t size = 54 + row * h;
    uint8_t hdr[54] = {'B', 'M'};
    auto le32 = [&](int off, uint32_t v) { memcpy(hdr + off, &v, 4); };
    le32(2, size);
    le32(10, 54);
    le32(14, 40);
    le32(18, w);
    le32(22, h);
    hdr[26] = 1;
    hdr[28] = 24;
    le32(34, row * h);
    server.setContentLength(size);
    server.send(200, "image/bmp", "");
    server.client().write(hdr, sizeof hdr);
    static uint8_t line[240 * 3 + 4];
    for (int32_t y = h - 1; y >= 0; y--) {
        const uint16_t *src = (const uint16_t *)(snap->data + y * snap->header.stride);
        memset(line, 0, row);
        for (uint32_t x = 0; x < w && x < 240; x++) {
            const uint16_t c = src[x];
            line[x * 3 + 0] = (c & 0x1F) << 3;
            line[x * 3 + 1] = ((c >> 5) & 0x3F) << 2;
            line[x * 3 + 2] = (c >> 11) << 3;
        }
        server.client().write(line, row);
    }
    lv_draw_buf_destroy(snap);
}

void handle_update_done() {
    if (!ota_authed) return;  // the upload handler already answered
    if (ota_failed || Update.hasError()) {
        server.send(500, "text/plain", String("update failed: ") + Update.errorString());
        return;
    }
    server.send(200, "text/plain", "update ok, restarting");
    restart_at = millis() + 800;
}

void handle_update_upload() {
    HTTPUpload &up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        ota_failed = false;
        ota_authed = authed();
        if (!ota_authed) return;
        logf("web: firmware upload %s", up.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) ota_failed = true;
    } else if (!ota_authed || ota_failed) {
        return;
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) ota_failed = true;
    } else if (up.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) ota_failed = true;
        logf("web: firmware upload %u bytes, %s", up.totalSize, ota_failed ? Update.errorString() : "ok");
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        ota_failed = true;
    }
}

}  // namespace

void web_begin(PrinterState &s) {
    S = &s;
    server.on("/", HTTP_GET, handle_root);
    server.on("/style.css", HTTP_GET, [] { server.send_P(200, "text/css", STYLE_CSS); });
    server.on("/api/setup", HTTP_POST, handle_setup);
    server.on("/api/login", HTTP_POST, handle_login);
    server.on("/api/logout", HTTP_POST, handle_logout);
    server.on("/api/status", HTTP_GET, handle_status);
    server.on("/api/config", HTTP_GET, handle_get_config);
    server.on("/api/config", HTTP_POST, handle_post_config);
    server.on("/api/scan", HTTP_GET, handle_scan);
    server.on("/api/log", HTTP_GET, handle_log);
    server.on("/api/backup", HTTP_GET, handle_backup);
    server.on("/api/boot-other", HTTP_POST, handle_boot_other);
    server.on("/api/restart", HTTP_POST, handle_restart);
    server.on("/api/tile", HTTP_POST, handle_tile);
    server.on("/api/screenshot.bmp", HTTP_GET, handle_screenshot);
    server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
    server.onNotFound([] { server.send(404, "text/plain", "not found"); });
    const char *headers[] = {"Cookie"};
    server.collectHeaders(headers, 1);
    server.begin();
}

void web_loop() {
    server.handleClient();
    if (restart_at && millis() >= restart_at) ESP.restart();
}
