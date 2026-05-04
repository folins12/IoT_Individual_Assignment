/*
 * FLOAT - Target Node
 * ====================
 * Responsibilities:
 *  - Read real turbidity via analog sensor (e.g. DFRobot SEN0189 / generic TSD-10)
 *    connected to GPIO 1 (ADC1_CH0).  Output is 0–4095 → converted to NTU.
 *  - Read water temperature via DS18B20 on GPIO 4 (shared OneWire bus)
 *  - Control the water pump (GPIO 47)
 *  - Control the servo feeder (GPIO 6, bit-bang PWM)
 *  - Send structured LOG and CMD messages to Observer via ESP-NOW
 *  - Receive HALT from Observer and enter deep sleep
 *
 * Turbidity sensor calibration
 * ------------------------------
 * The SEN0189 / generic voltage-output turbidity sensor produces:
 *   ~4.2 V (≈ 3435 ADC counts on 3.3 V ESP32 ADC) in clean water
 *   ~2.5 V (≈ 2048 counts) at ~3000 NTU
 *
 * We use a two-point linear mapping:
 *   NTU = (CLEAN_ADC - adc_reading) * NTU_PER_COUNT
 *
 * A NTU > TURB_THRESHOLD triggers the pump cycle.
 * Adjust TURB_CLEAN_ADC and TURB_DIRTY_ADC for your specific sensor.
 *
 * Battery life estimate  (answers professor Q3)
 * -----------------------------------------------
 *  Component         Active current    Sleep current
 *  ESP32-S3          ~80 mA            ~10 µA
 *  INA219 (observer) ~1 mA             (N/A — on observer board)
 *  DS18B20           ~1.5 mA           ~1 µA
 *  Pump              ~350 mA (only when running, gated by MOSFET)
 *
 * Duty cycle: 10 s active, 10 s deep sleep → 50 % sleep fraction.
 *
 * Average node current (excluding pump):
 *   I_avg = 0.5 × 80 mA + 0.5 × 0.010 mA ≈ 40 mA
 *
 * With a 3 000 mAh LiPo:
 *   T = 3000 / 40 ≈ 75 h  ≈  ~3 days  (node alone, no pump)
 *
 * The pump runs ~10 s every wake cycle only when turbidity > threshold.
 * Assuming it runs 20 % of cycles: pump contribution ≈ 0.5 × 0.2 × 350 = 35 mA
 *   Total I_avg with pump ≈ 75 mA  →  T ≈ 40 h  ≈  ~1.7 days
 *
 * "20 days" is NOT achievable with this hardware on a 3 000 mAh cell.
 * To reach 20 days (480 h) the average current must be ≤ 6.25 mA,
 * requiring either:
 *   a) a much larger battery (~2 000 mAh @ 6 mA → feasible with 30 000 mAh pack)
 *   b) a much lower active duty cycle (e.g. 30 s sleep, 2 s active → I_avg ≈ 5 mA)
 *   c) a low-power MCU (e.g. ESP32-C3 in modem-sleep or LoRa node)
 *
 * The claim of 20 days should be revised or backed with a specific battery spec.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ── Pin map ────────────────────────────────────────────────────────────────
const int PUMP_PIN       = 47;
const int SERVO_PIN      = 6;
const int TURBIDITY_PIN  = 1;   // ADC1_CH0 — analog turbidity sensor output
const int DS18B20_PIN    = 4;   // OneWire (4.7 kΩ pull-up to 3.3 V)

// ── Turbidity sensor calibration ──────────────────────────────────────────
// Tune these two constants against your actual sensor in clear water and
// in water of known NTU (a standard NTU solution, or compare vs a meter).
const int   TURB_CLEAN_ADC    = 3435;   // ADC count in distilled water (≈ 0 NTU)
const int   TURB_DIRTY_ADC    = 1200;   // ADC count at ~3000 NTU reference
const float TURB_MAX_NTU      = 3000.0f;
const float TURB_THRESHOLD_NTU = 100.0f; // NTU above which pump activates

// ── RTC-persistent state (survives deep sleep) ─────────────────────────────
RTC_DATA_ATTR int  bootCount      = 0;
RTC_DATA_ATTR bool system_halted  = false;

// ── Globals ────────────────────────────────────────────────────────────────
uint8_t observerAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
volatile bool emergency_stop = false;

OneWire           oneWire(DS18B20_PIN);
DallasTemperature tempSensor(&oneWire);

// ── ESP-NOW callback ───────────────────────────────────────────────────────
void OnDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    char msg[len + 1];
    memcpy(msg, data, len);
    msg[len] = '\0';
    if (String(msg) == "HALT") {
        digitalWrite(PUMP_PIN, LOW);
        emergency_stop = true;
        system_halted  = true;
    }
}

// ── Helpers ────────────────────────────────────────────────────────────────
void sendMsg(const String& type, const String& content) {
    String full = type + ":" + content;
    esp_now_send(observerAddress, (const uint8_t*)full.c_str(), full.length());
    delay(80);
}

/**
 * Read turbidity via ADC and return NTU.
 * Takes 16 averaged samples to reduce ADC noise on ESP32.
 */
float readTurbidityNTU() {
    long sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogRead(TURBIDITY_PIN);
        delay(2);
    }
    int adc = (int)(sum / 16);

    // Clamp to calibration range
    adc = constrain(adc, TURB_DIRTY_ADC, TURB_CLEAN_ADC);

    // Linear interpolation: lower ADC → higher turbidity
    float ntu = TURB_MAX_NTU *
                (float)(TURB_CLEAN_ADC - adc) /
                (float)(TURB_CLEAN_ADC - TURB_DIRTY_ADC);
    return constrain(ntu, 0.0f, TURB_MAX_NTU);
}

/** Bit-bang servo: send `pulses` PWM pulses of `us` microseconds. */
void servoMove(int us, int pulses) {
    for (int i = 0; i < pulses; i++) {
        digitalWrite(SERVO_PIN, HIGH);
        delayMicroseconds(us);
        digitalWrite(SERVO_PIN, LOW);
        delay(18);
    }
}

// ── setup ──────────────────────────────────────────────────────────────────
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);   // disable brownout reset

    Serial.begin(115200);
    pinMode(PUMP_PIN,  OUTPUT);
    pinMode(SERVO_PIN, OUTPUT);
    analogReadResolution(12);                      // 12-bit ADC (0–4095)

    digitalWrite(PUMP_PIN,  LOW);
    digitalWrite(SERVO_PIN, LOW);

    // If a HALT was received last cycle, stay asleep
    if (system_halted) {
        Serial.println("[TGT] System halted — staying in deep sleep.");
        esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL);
        esp_deep_sleep_start();
    }

    // ── Sensors ────────────────────────────────────────────────────────────
    tempSensor.begin();
    tempSensor.setResolution(11);
    tempSensor.requestTemperatures();
    delay(400);   // allow DS18B20 conversion
    float water_temp = tempSensor.getTempCByIndex(0);
    if (water_temp == DEVICE_DISCONNECTED_C || water_temp < -10.0f)
        water_temp = 25.0f;   // fallback if probe absent

    float turbidity_ntu = readTurbidityNTU();
    int   turbidity_pct = (int)((turbidity_ntu / TURB_MAX_NTU) * 100.0f);

    // ── ESP-NOW ────────────────────────────────────────────────────────────
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setTxPower(WIFI_POWER_2dBm);

    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
        esp_now_peer_info_t peer;
        memset(&peer, 0, sizeof(peer));
        memcpy(peer.peer_addr, observerAddress, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    delay(2000);   // allow ESP-NOW stack to settle

    // ── Log sensor readings ────────────────────────────────────────────────
    sendMsg("LOG", "────────────────────────────────────");
    sendMsg("LOG", "[BOOT] #" + String(bootCount));
    sendMsg("LOG", "[SENSOR] Turbidity : " + String(turbidity_ntu, 1) + " NTU  (" + String(turbidity_pct) + "%)");
    sendMsg("LOG", "[SENSOR] Water Temp: " + String(water_temp, 1) + " °C");

    // Forward sensor pack to observer so dashboard stays in sync
    String sensorPack = "SENSOR:" +
                        String(turbidity_ntu, 1) + "," +
                        String(water_temp, 1);
    sendMsg("DATA", sensorPack);

    // ── Decision logic ─────────────────────────────────────────────────────
    if (bootCount == 0) {
        // ── First boot: calibration learning phase ─────────────────────────
        sendMsg("LOG", "[ACTION] First boot → calibration learning phase");
        sendMsg("CMD", "START_LEARN");
        delay(500);

        digitalWrite(PUMP_PIN, HIGH);
        unsigned long t0 = millis();
        while (millis() - t0 < 10000 && !emergency_stop) delay(10);
        digitalWrite(PUMP_PIN, LOW);

        sendMsg("CMD", "STOP_MEASURE");

    } else if (turbidity_ntu > TURB_THRESHOLD_NTU) {
        // ── Water dirty: run pump under monitoring ─────────────────────────
        sendMsg("LOG", "[ACTION] Turbidity=" + String(turbidity_ntu, 1) +
                       " NTU > threshold → pump ON (10 s)");
        sendMsg("CMD", "START_MONITOR");
        delay(500);

        digitalWrite(PUMP_PIN, HIGH);
        unsigned long t0 = millis();
        while (millis() - t0 < 10000 && !emergency_stop) delay(10);
        digitalWrite(PUMP_PIN, LOW);

        sendMsg("CMD", "STOP_MEASURE");

    } else {
        // ── Water clean: dispense food ─────────────────────────────────────
        sendMsg("LOG", "[ACTION] Water clean (" +
                       String(turbidity_ntu, 1) + " NTU) → dispensing food");

        sendMsg("LOG", "[SERVO] Open 90°");
        servoMove(1500, 35);   // 1500 µs ≈ 90°
        delay(1000);

        sendMsg("LOG", "[SERVO] Close 0°");
        servoMove(1000, 35);   // 1000 µs ≈ 0°
    }

    // ── Sleep ──────────────────────────────────────────────────────────────
    bootCount++;
    sendMsg("LOG", "[SLEEP] Deep sleep for 10 s...");
    delay(200);
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {}