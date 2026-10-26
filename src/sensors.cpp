#include "sensors.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"
#include "log.h"

namespace {

bool sht_ok, acc_ok;
uint32_t sht_next, sht_ready_at, acc_next, knock_quiet_until;
float base[3];
bool base_set;

bool write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool read_regs(uint8_t addr, uint8_t reg, uint8_t *out, uint8_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, n) != n) return false;
    for (uint8_t i = 0; i < n; i++) out[i] = Wire.read();
    return true;
}

bool sht_start() {
    Wire.beginTransmission(ADDR_SHT4X);
    Wire.write(0xFD);  // measure T and RH, high precision (~9 ms)
    return Wire.endTransmission() == 0;
}

}  // namespace

void sensors_begin() {
    sht_ok = sht_start();
    sht_ready_at = millis() + 12;
    uint8_t who = 0;
    acc_ok = read_regs(ADDR_LIS2DW, 0x0F, &who, 1) && who == 0x44;
    if (acc_ok) {
        write_reg(ADDR_LIS2DW, 0x20, 0x54);  // CTRL1: 100 Hz, high performance
        write_reg(ADDR_LIS2DW, 0x25, 0x00);  // CTRL6: +-2 g
    }
    logf("sensors: SHT4x %s, LIS2DW12 %s", sht_ok ? "ok" : "missing", acc_ok ? "ok" : "missing");
}

bool sensors_loop(uint32_t now, PrinterState &s) {
    if (sht_ok && sht_ready_at && now >= sht_ready_at) {
        sht_ready_at = 0;
        uint8_t r[6];
        if (Wire.requestFrom((uint8_t)ADDR_SHT4X, (uint8_t)6) == 6) {
            for (uint8_t &b : r) b = Wire.read();
            const uint16_t t = r[0] << 8 | r[1], h = r[3] << 8 | r[4];
            s.air_temp = -45 + 175.0f * t / 65535;
            s.air_hum = fminf(100, fmaxf(0, -6 + 125.0f * h / 65535));
            s.has_air = true;
        }
        sht_next = now + 5000;
    }
    if (sht_ok && !sht_ready_at && now >= sht_next) {
        if (sht_start()) sht_ready_at = now + 12;
        else sht_next = now + 5000;
    }

    bool knock = false;
    if (acc_ok && now >= acc_next) {
        acc_next = now + 20;
        uint8_t r[6];
        if (read_regs(ADDR_LIS2DW, 0x28, r, 6)) {
            float a[3];
            for (int i = 0; i < 3; i++) a[i] = (int16_t)(r[2 * i] | r[2 * i + 1] << 8) / 16384.0f;
            if (!base_set) {
                memcpy(base, a, sizeof base);
                base_set = true;
            }
            const float d = fabsf(a[0] - base[0]) + fabsf(a[1] - base[1]) + fabsf(a[2] - base[2]);
            // Motion without gravity for the eyes: sensor x runs across the
            // screen, sensor z through it (toolhead front/back on a toolhead
            // mount; BTT's firmware swaps y and z for the same reason).
            s.has_motion = true;
            s.motion[0] += ((a[0] - base[0]) - s.motion[0]) * 0.3f;
            s.motion[1] += ((a[2] - base[2]) - s.motion[1]) * 0.3f;
            for (int i = 0; i < 3; i++) base[i] += (a[i] - base[i]) * 0.02f;
            // A sharp jolt while the printer itself is still (its own moves
            // shake the KNOMI too when it sits on the frame or gantry).
            const bool printer_still = !s.busy && s.job != Job::Printing && s.velocity < 1;
            if (d > 0.45f && printer_still && now >= knock_quiet_until) {
                knock = true;
                knock_quiet_until = now + 2000;
            }
        }
    }
    return knock;
}
