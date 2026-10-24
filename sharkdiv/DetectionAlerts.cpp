#include <Arduino.h>
#include "DetectionAlerts.h"
#include "shared.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace DetectionAlerts {
// In Arduino ESP32 2.x, adjacent LEDC channels share a timer. Backlight uses
// channel 0/timer 0: channel 1 would change its frequency and resolution.
static constexpr uint8_t buzzerChannel = 2;
static_assert((buzzerChannel / 2) != (PWM_CHANNEL / 2), "Buzzer must not share the backlight timer");
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Policy policy;
static bool on = true, ready = false, pending = false, testPending = false;
static Source pendingSource = Source::WiFi;
static uint32_t pendingAt = 0, generation = 0, played = 0;
static TaskHandle_t task = nullptr;

uint32_t hash(const uint8_t* data, size_t length) {
  uint32_t value = 2166136261UL;
  if (!data) return value;
  for (size_t i = 0; i < length; ++i) value = (value ^ data[i]) * 16777619UL;
  return value;
}

static bool doubleChirp(Source source) {
  return source == Source::Flock || source == Source::Camera ||
         source == Source::Gear || source == Source::FindMy || source == Source::Drone;
}
static uint16_t frequency(Source source) {
  if (doubleChirp(source)) return 2600;
  if (source == Source::SubGhz || source == Source::RF24) return 1600;
  return 2100;
}

static void audioTask(void*) {
  uint8_t phase = 0;
  uint32_t phaseAt = 0, playingGeneration = 0;
  bool activeBefore = false, previewing = false;
  Source source = Source::WiFi;
  for (;;) {
    const uint32_t now = millis();
    bool cancel = false, start = false;
    const bool active = feature_active;
    portENTER_CRITICAL(&mux);
    if (activeBefore && !active) { policy.reset(); if (!testPending) pending = false; }
    cancel = !on || generation != playingGeneration || (!active && !previewing);
    if (pending && (!on || uint32_t(now - pendingAt) > 250 || (!active && !testPending))) pending = false;
    if (!phase && pending && on) {
      source = pendingSource;
      previewing = testPending;
      playingGeneration = generation;
      pending = testPending = false;
      start = true;
      ++played;
    }
    portEXIT_CRITICAL(&mux);
    activeBefore = active;
    if (phase && cancel) { ledcWrite(buzzerChannel, 0); phase = 0; previewing = false; }
    if (start) {
      ledcWriteTone(buzzerChannel, frequency(source));
      phase = 1; phaseAt = now;
    } else if (phase == 1 && uint32_t(now - phaseAt) >= 65) {
      ledcWrite(buzzerChannel, 0);
      phase = doubleChirp(source) ? 2 : 0; phaseAt = now;
    } else if (phase == 2 && uint32_t(now - phaseAt) >= 75) {
      ledcWriteTone(buzzerChannel, frequency(source));
      phase = 3; phaseAt = now;
    } else if (phase == 3 && uint32_t(now - phaseAt) >= 65) {
      ledcWrite(buzzerChannel, 0); phase = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void begin(bool value) {
  setEnabled(value);
  if (task) return;
#if !DIV_VM_BUILD
  if (!GPIO_IS_VALID_OUTPUT_GPIO(BUZZER_PIN) || BUZZER_PIN == BACKLIGHT_PIN) return;
  if (ledcSetup(buzzerChannel, 2100, 10) == 0) return;
  ledcAttachPin(BUZZER_PIN, buzzerChannel);
  ledcWrite(buzzerChannel, 0);
  const bool created = xTaskCreate(audioTask, "alerts", 2048, nullptr, 1, &task) == pdPASS;
  portENTER_CRITICAL(&mux);
  ready = created;
  portEXIT_CRITICAL(&mux);
  if (!created) { ledcWrite(buzzerChannel, 0); ledcDetachPin(BUZZER_PIN); }
#endif
  printStatus();
}
void setEnabled(bool value) {
  portENTER_CRITICAL(&mux);
  on = value;
  pending = testPending = false;
  ++generation;
  policy.reset();
  portEXIT_CRITICAL(&mux);
}
bool enabled() { portENTER_CRITICAL(&mux); const bool value = on; portEXIT_CRITICAL(&mux); return value; }
bool available() { portENTER_CRITICAL(&mux); const bool value = ready; portEXIT_CRITICAL(&mux); return value; }
void notify(Source source, uint32_t identity) {
  if (!feature_active) return;
  const uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  if (ready && on && policy.offer(source, identity, now)) {
    pendingSource = source; pendingAt = now; pending = true; testPending = false;
  }
  portEXIT_CRITICAL(&mux);
}
bool preview() {
  const uint32_t now = millis();
  portENTER_CRITICAL(&mux);
  const bool ok = ready && on;
  if (ok) { pendingSource = Source::Flock; pendingAt = now; pending = testPending = true; }
  portEXIT_CRITICAL(&mux);
  return ok;
}
void printStatus() {
  portENTER_CRITICAL(&mux);
  const bool isReady = ready, isOn = on;
  const uint32_t count = played;
  portEXIT_CRITICAL(&mux);
  Serial.printf("[sound] available=%d enabled=%d alerts=%lu pin=%d channel=%u backlight=%luHz\n",
                isReady, isOn, (unsigned long)count, BUZZER_PIN, buzzerChannel,
                (unsigned long)ledcReadFreq(PWM_CHANNEL));
}
}
