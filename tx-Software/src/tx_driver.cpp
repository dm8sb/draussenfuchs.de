// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Harm  , DK4HAA
// Copyright (c) 2026 Sebastian , DM8SB

// ============================================================================
// tx_driver.cpp – TX-Hardware-Treiber Implementierung
// ============================================================================

#include "tx_driver.h"
#include "config.h"
#include <Arduino.h>
#include <driver/gpio.h>

namespace ardf {
namespace tx {

// ---- LEDC-Konfiguration ----
// Kompatibilitätsschicht: Core 2.x (kanalbasiert) und Core 3.x (pin-basiert)
// Die Plattform espressif32 ^6.8.0 liefert Core 2.x mit ledcSetup/ledcAttachPin.
// Sollte eine spätere Version Core 3.x liefern, wird ledcAttach/ledcDetach verwendet.
#include <esp_arduino_version.h>

static constexpr uint8_t LEDC_CHANNEL = 0;
static constexpr uint8_t LEDC_RESOLUTION = 8;  // 8 Bit
static bool ledc_attached = false;

void init() {
    pinMode(TX_VCC_EN_PIN, OUTPUT);
    digitalWrite(TX_VCC_EN_PIN, LOW);  // TX-Modul stromlos

    pinMode(DATA_PIN, OUTPUT);
    digitalWrite(DATA_PIN, LOW);

    // Pin-Hold aus dem Deep-Sleep aufheben (erst nach LOW-Konfiguration,
    // damit beim Freigeben kein Pegelwechsel entsteht)
    gpio_hold_dis((gpio_num_t)TX_VCC_EN_PIN);
    gpio_hold_dis((gpio_num_t)DATA_PIN);
    gpio_deep_sleep_hold_dis();

    // LEDs (active LOW)
    pinMode(LED_PIN_1, OUTPUT);
    digitalWrite(LED_PIN_1, HIGH);  // LED aus
    pinMode(LED_PIN_2, OUTPUT);
    digitalWrite(LED_PIN_2, HIGH);  // LED aus

    ledc_attached = false;
}

void power(bool on) {
    if (on) {
        digitalWrite(TX_VCC_EN_PIN, HIGH);
    } else {
        digitalWrite(TX_VCC_EN_PIN, LOW);
    }
}

void ledOn() {
    // Active LOW: LOW = LED an
    digitalWrite(LED_PIN_1, LOW);
    digitalWrite(LED_PIN_2, LOW);
}

void ledOff() {
    // Active LOW: HIGH = LED aus
    digitalWrite(LED_PIN_1, HIGH);
    digitalWrite(LED_PIN_2, HIGH);
}

void toneOn(uint16_t freq_hz) {
    // LEDC-Ton einschalten
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    // Core 3.x: pin-basierte API
    if (!ledc_attached) {
        ledcAttach(DATA_PIN, freq_hz, LEDC_RESOLUTION);
        ledc_attached = true;
    }
    ledcWriteTone(DATA_PIN, freq_hz);
#else
    // Core 2.x: kanalbasierte API
    if (!ledc_attached) {
        ledcSetup(LEDC_CHANNEL, freq_hz, LEDC_RESOLUTION);
        ledcAttachPin(DATA_PIN, LEDC_CHANNEL);
        ledc_attached = true;
    }
    ledcWriteTone(LEDC_CHANNEL, freq_hz);
#endif
    ledOn();
}

void toneOff() {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(DATA_PIN, 0);
#else
    ledcWrite(LEDC_CHANNEL, 0);
#endif
    ledOff();
}

// ---- LED-Blinkmuster ----

void blinkSos() {
    // SOS: ...---...
    uint16_t dit_time = 150;
    uint16_t dah_time = 450;
    uint16_t pause = 150;

    // S: ...
    for (int i = 0; i < 3; i++) {
        ledOn(); delay(dit_time);
        ledOff(); delay(pause);
    }
    delay(pause * 2); // Zeichenabstand

    // O: ---
    for (int i = 0; i < 3; i++) {
        ledOn(); delay(dah_time);
        ledOff(); delay(pause);
    }
    delay(pause * 2);

    // S: ...
    for (int i = 0; i < 3; i++) {
        ledOn(); delay(dit_time);
        ledOff(); delay(pause);
    }

    delay(1200); // Pause nach SOS
}

void blinkMismatch() {
    // 3× kurz (150 ms an / 150 ms aus), Pause 1 s
    for (int i = 0; i < 3; i++) {
        ledOn(); delay(150);
        ledOff(); delay(150);
    }
    delay(1000);
}

void blinkInvalid() {
    // 10 Hz Dauerblinken (50 ms an / 50 ms aus)
    ledOn(); delay(50);
    ledOff(); delay(50);
}

void blinkDutyBlocked() {
    // 2× kurz, Pause 2 s
    for (int i = 0; i < 2; i++) {
        ledOn(); delay(150);
        ledOff(); delay(150);
    }
    delay(2000);
}

void blinkHeartbeat() {
    // 2 Hz Herzschlag (250 ms an / 250 ms aus)
    ledOn(); delay(250);
    ledOff(); delay(250);
}

void blinkIdle() {
    // 0,5 Hz langsam (500 ms an / 1500 ms aus)
    ledOn(); delay(500);
    ledOff(); delay(1500);
}

} // namespace tx
} // namespace ardf
