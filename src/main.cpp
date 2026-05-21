// RefinePS firmware — ESP32 BLE GATT server with LEDC PWM control + WiFi OTA
// Service:  000000FF-0000-1000-8000-00805F9B34FB
// Write:    0000FF01  (WRITE_NR, JSON command terminated by \n)
// Notify:   0000FF02  (status reply)
//
// Set WIFI_SSID / WIFI_PASS below before flashing.
// OTA hostname: refineps  — flash via: pio run -t upload --upload-port refineps.local

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "driver/ledc.h"
#include <WiFi.h>
#include <ArduinoOTA.h>

#define SERVICE_UUID     "000000FF-0000-1000-8000-00805F9B34FB"
#define WRITE_CHAR_UUID  "0000FF01-0000-1000-8000-00805F9B34FB"
#define NOTIFY_CHAR_UUID "0000FF02-0000-1000-8000-00805F9B34FB"

#define PWM_A_PIN   25
#define PWM_B_PIN   26
#define PWM_RES     8    // 8-bit; suits 100 kHz on 80 MHz APB

#define WIFI_SSID   "your_ssid"
#define WIFI_PASS   "your_password"

static NimBLECharacteristic* pNotifyChar = nullptr;
static String rxBuf;

// ── LEDC helper ────────────────────────────────────────────────────────────

static void applyChannel(int ch_num, int pin, int duty_pct, int freq_hz) {
    if (freq_hz <= 0) freq_hz = 100000;
    ledc_timer_config_t tmr = {};
    tmr.speed_mode      = LEDC_LOW_SPEED_MODE;
    tmr.duty_resolution = (ledc_timer_bit_t)PWM_RES;
    tmr.timer_num       = (ledc_timer_t)ch_num;
    tmr.freq_hz         = (uint32_t)freq_hz;
    tmr.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&tmr);

    ledc_channel_config_t ch = {};
    ch.gpio_num   = pin;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = (ledc_channel_t)ch_num;
    ch.intr_type  = LEDC_INTR_DISABLE;
    ch.timer_sel  = (ledc_timer_t)ch_num;
    ch.duty       = (uint32_t)(duty_pct * ((1 << PWM_RES) - 1) / 100);
    ch.hpoint     = 0;
    ledc_channel_config(&ch);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)ch_num);
}

// ── JSON value extraction ──────────────────────────────────────────────────

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

    int duty_a       = constrain(extractInt(line, "\"duty_a\"",       0), 0, 100);
    int duty_b       = constrain(extractInt(line, "\"duty_b\"",       0), 0, 100);
    int freq_carrier = extractInt(line, "\"freq_carrier\"", 100000);
    int freq_switch  = extractInt(line, "\"freq_switch\"",  0);

    applyChannel(0, PWM_A_PIN, duty_a, freq_carrier);
    applyChannel(1, PWM_B_PIN, duty_b, freq_carrier);

    String resp = String(duty_a) + " fs=" + String(freq_switch) + "\n";
    Serial.print(resp);

    if (pNotifyChar && NimBLEDevice::getServer()->getConnectedCount() > 0) {
        pNotifyChar->setValue(resp);
        pNotifyChar->notify();
    }
}

// ── BLE callbacks ──────────────────────────────────────────────────────────

class ServerCallbacks : public NimBLEServerCallbacks {
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        NimBLEDevice::getAdvertising()->start();
    }
};

class WriteCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
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

    // GPIO 25/26 are DAC pins — disable DAC before LEDC takes over
    dacDisable(PWM_A_PIN);
    dacDisable(PWM_B_PIN);

    // WiFi + OTA
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setHostname("refineps");
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi: "); Serial.println(WiFi.localIP());
        ArduinoOTA.setHostname("refineps");
        ArduinoOTA.begin();
        Serial.println("OTA ready");
    } else {
        Serial.println("WiFi not connected — OTA unavailable");
    }

    // BLE
    NimBLEDevice::init("REFINEPS");
    NimBLEServer*  pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    NimBLEService* pSvc = pServer->createService(SERVICE_UUID);

    NimBLECharacteristic* pWriteChar = pSvc->createCharacteristic(
        WRITE_CHAR_UUID, NIMBLE_PROPERTY::WRITE_NR);
    pWriteChar->setCallbacks(new WriteCallback());

    pNotifyChar = pSvc->createCharacteristic(
        NOTIFY_CHAR_UUID, NIMBLE_PROPERTY::NOTIFY);

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(SERVICE_UUID);
    pAdv->start();

    Serial.println("REFINEPS ready");
}

void loop() {
    ArduinoOTA.handle();
    delay(10);
}
