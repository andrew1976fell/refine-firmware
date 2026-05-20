// RefinePS firmware — ESP32 BLE GATT server with LEDC PWM control
// Service:  000000FF-0000-1000-8000-00805F9B34FB
// Write:    0000FF01  (WRITE_NR, JSON command terminated by \n)
// Notify:   0000FF02  (status reply)
//
// Command JSON: {"magic":"refine","cmd":"set","ch":1,"duty_a":<0-100>,
//               "duty_b":<0-100>,"freq_carrier":<Hz>,"freq_switch":<Hz>}
//
// Adjust PWM_A_PIN / PWM_B_PIN to match your hardware wiring.

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "driver/ledc.h"

#define SERVICE_UUID     "000000FF-0000-1000-8000-00805F9B34FB"
#define WRITE_CHAR_UUID  "0000FF01-0000-1000-8000-00805F9B34FB"
#define NOTIFY_CHAR_UUID "0000FF02-0000-1000-8000-00805F9B34FB"

#define PWM_A_PIN   18   // change to match hardware
#define PWM_B_PIN   19   // change to match hardware
#define PWM_RES     8    // 8-bit: 0-255 duty counts; suits 100 kHz on 80 MHz APB

static NimBLECharacteristic* pNotifyChar = nullptr;
static String rxBuf;

// ── LEDC helper ────────────────────────────────────────────────────────────

static void applyChannel(int nimbleChannel, int pin, int duty_pct, int freq_hz) {
    if (freq_hz <= 0) freq_hz = 100000;
    ledc_timer_config_t tmr = {};
    tmr.speed_mode       = LEDC_LOW_SPEED_MODE;
    tmr.duty_resolution  = (ledc_timer_bit_t)PWM_RES;
    tmr.timer_num        = (ledc_timer_t)nimbleChannel;
    tmr.freq_hz          = (uint32_t)freq_hz;
    tmr.clk_cfg          = LEDC_AUTO_CLK;
    ledc_timer_config(&tmr);

    ledc_channel_config_t ch = {};
    ch.gpio_num   = pin;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = (ledc_channel_t)nimbleChannel;
    ch.intr_type  = LEDC_INTR_DISABLE;
    ch.timer_sel  = (ledc_timer_t)nimbleChannel;
    ch.duty       = (uint32_t)(duty_pct * ((1 << PWM_RES) - 1) / 100);
    ch.hpoint     = 0;
    ledc_channel_config(&ch);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)nimbleChannel);
}

// ── JSON value extraction (no extra library) ───────────────────────────────

static int extractInt(const String& json, const char* key, int def) {
    int idx = json.indexOf(key);
    if (idx < 0) return def;
    idx = json.indexOf(':', idx);
    if (idx < 0) return def;
    idx++;
    while (idx < (int)json.length() && json[idx] == ' ') idx++;
    return json.substring(idx).toInt();
}

// ── Command handler ────────────────────────────────────────────────────────

static void handleLine(const String& line) {
    if (line.indexOf("\"magic\"") < 0) return;

    int duty_a       = extractInt(line, "\"duty_a\"",       0);
    int duty_b       = extractInt(line, "\"duty_b\"",       0);
    int freq_carrier = extractInt(line, "\"freq_carrier\"", 100000);
    int freq_switch  = extractInt(line, "\"freq_switch\"",  0);

    duty_a = constrain(duty_a, 0, 100);
    duty_b = constrain(duty_b, 0, 100);

    applyChannel(0, PWM_A_PIN, duty_a, freq_carrier);
    applyChannel(1, PWM_B_PIN, duty_b, freq_carrier);

    String resp = String(duty_a) + " fs=" + String(freq_switch) + "\n";
    Serial.print(resp);

    if (pNotifyChar && NimBLEDevice::getServer()->getConnectedCount() > 0) {
        pNotifyChar->setValue(resp);
        pNotifyChar->notify();
    }
}

// ── BLE write callback ─────────────────────────────────────────────────────

class WriteCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        rxBuf += pChar->getValue().c_str();
        int nl;
        while ((nl = rxBuf.indexOf('\n')) >= 0) {
            String line = rxBuf.substring(0, nl);
            rxBuf.remove(0, nl + 1);
            line.trim();
            if (line.length()) handleLine(line);
        }
    }
};

// ── setup / loop ───────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    NimBLEDevice::init("REFINEPS");
    NimBLEServer*  pServer = NimBLEDevice::createServer();
    NimBLEService* pSvc    = pServer->createService(SERVICE_UUID);

    NimBLECharacteristic* pWriteChar = pSvc->createCharacteristic(
        WRITE_CHAR_UUID, NIMBLE_PROPERTY::WRITE_NR);
    pWriteChar->setCallbacks(new WriteCallback());

    pNotifyChar = pSvc->createCharacteristic(
        NOTIFY_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

    pSvc->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->start();

    Serial.println("REFINEPS ready");
}

void loop() {
    delay(10);
}
