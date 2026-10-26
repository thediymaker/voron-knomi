#include "net.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include "config.h"
#include "log.h"

namespace {

const uint32_t AP_AFTER_MS = 30000;  // without a connection
uint32_t lost_since;
bool ap_on, mdns_on, was_connected;

void start_ap(PrinterState &s) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(s.ap_ssid, sizeof s.ap_ssid, "KNOMI-%02X%02X", mac[4], mac[5]);
    // Fresh password every time, readable only from the screen.
    static const char chars[] = "abcdefghjkmnpqrstuvwxyz23456789";
    for (int i = 0; i < 9; i++) s.ap_pass[i] = i == 4 ? '-' : chars[esp_random() % (sizeof chars - 1)];
    s.ap_pass[9] = 0;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(s.ap_ssid, s.ap_pass);
    ap_on = true;
    s.ap_mode = true;
    logf("wifi: setup access point %s at %s", s.ap_ssid, WiFi.softAPIP().toString().c_str());
}

void stop_ap(PrinterState &s) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    ap_on = false;
    s.ap_mode = false;
    s.ap_pass[0] = 0;
    logf("wifi: setup access point closed");
}

}  // namespace

void net_begin(PrinterState &s) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(cfg.hostname);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    if (cfg.wifi_ssid[0]) {
        logf("wifi: joining '%s'", cfg.wifi_ssid);
        WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);
    }
    lost_since = millis();
    if (!cfg.wifi_ssid[0]) start_ap(s);
}

void net_reconnect() {
    WiFi.disconnect();
    WiFi.setHostname(cfg.hostname);
    if (cfg.wifi_ssid[0]) WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);
    lost_since = millis();
}

void net_loop(uint32_t now, PrinterState &s) {
    const bool up = WiFi.status() == WL_CONNECTED;
    s.wifi = up;
    if (up && !was_connected) {
        snprintf(s.ip, sizeof s.ip, "%s", WiFi.localIP().toString().c_str());
        logf("wifi: connected, %s, RSSI %d", s.ip, WiFi.RSSI());
        if (!mdns_on && MDNS.begin(cfg.hostname)) {
            MDNS.addService("http", "tcp", 80);
            mdns_on = true;
        }
    }
    if (!up && was_connected) {
        logf("wifi: connection lost");
        lost_since = now;
    }
    was_connected = up;

    if (!up && !ap_on && now - lost_since > AP_AFTER_MS) start_ap(s);
    // Close the setup AP once we are back on the network and nobody is using it.
    if (up && ap_on && WiFi.softAPgetStationNum() == 0) stop_ap(s);
}
