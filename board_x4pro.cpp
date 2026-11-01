// Standalone X4 Pro hardware. FreeInk owns the panel, touch and power drivers.
#if defined(SH_X4_PRO) && !defined(LGFX_SDL)
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <BatteryMonitor.h>
#include <FrontlightManager.h>
#include <PowerManager.h>
#include <atomic>
#include "x4.h"
#include "pro_power_button.h"

void crosswinds_touch_tap(int x, int y);
void crosswinds_touch_swipe(int sx, int sy, int ex, int ey);

namespace {
// Legacy constructor pins are ignored; the SDK uses BoardConfig::ACTIVE.
EInkDisplay panel(-1, -1, -1, -1, -1, -1);
InputManager input;
BatteryMonitor battery;
FrontlightManager light;
bool panelReady = false;
bool inputReady = false;
bool charging = false;
int batteryPercent = 100;
uint32_t lastActivity = 0, lastBattery = 0;
constexpr uint32_t IDLE_MS = 5u * 60u * 1000u;
enum class Kind : uint8_t { Button, Tap, Swipe };
struct Event {
  Kind kind;
  uint8_t button = 0;
  int16_t x = 0, y = 0, ex = 0, ey = 0;
};
QueueHandle_t events = nullptr;
TaskHandle_t sampler = nullptr;
std::atomic<uint8_t> held{0};
std::atomic<bool> stopping{false};
std::atomic<bool> stopped{false};
std::atomic<bool> powerPending{false};

int16_t pixel(float n, int size) { return (int16_t)constrain((int)(n * size), 0, size - 1); }

void sample(void*) {
  ProPowerButton power;
  for (;;) {
    if (stopping.load()) {
      stopped.store(true);
      while (stopping.load()) vTaskDelay(pdMS_TO_TICKS(15));
      stopped.store(false);
    }
    input.update();
    uint8_t mask = 0;
    for (uint8_t b = 0; b < X4_BTN_COUNT; ++b)
      if (input.isPressed(b)) mask |= 1u << b;
    held.store(mask);
    for (uint8_t b : {InputManager::BTN_UP, InputManager::BTN_DOWN}) {
      if (input.wasPressed(b)) {
        Event event{Kind::Button, (uint8_t)(b == InputManager::BTN_UP ? X4_BTN_UP : X4_BTN_DOWN)};
        xQueueSend(events, &event, 0);
      }
    }
    if (input.wasHomeKeyTapped()) {
      Event event{Kind::Button, X4_BTN_BACK};
      xQueueSend(events, &event, 0);
    }
    if (power.update(input.isPowerButtonPhysicallyPressed(), input.isPressed(InputManager::BTN_POWER),
                     input.wasPressed(InputManager::BTN_POWER), input.wasReleased(InputManager::BTN_POWER)))
      powerPending.store(true);
    float x, y, ex, ey;
    if (input.wasSwipe(x, y, ex, ey)) {
      Event event{Kind::Swipe, 0, pixel(x, IW), pixel(y, IH), pixel(ex, IW), pixel(ey, IH)};
      xQueueSend(events, &event, 0);
    } else if (input.wasTouchTap(x, y)) {
      Event event{Kind::Tap, 0, pixel(x, IW), pixel(y, IH)};
      xQueueSend(events, &event, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}
}  // namespace

uint8_t* x4pro_framebuffer() {
  if (!panelReady) {
    panel.begin();
    panelReady = panel.getFrameBuffer() != nullptr;
  }
  return panel.getFrameBuffer();
}
void x4pro_display_push(bool full) {
  if (panelReady) panel.displayBuffer(full ? EInkDisplay::HALF_REFRESH : EInkDisplay::FAST_REFRESH);
}

void board_x4_begin_early() {
  BoardConfig::holdPowerRails();
  BoardConfig::releaseSdRail();
}
void board_x4_begin() {
  input.begin();
#ifdef FREEINK_FRONTLIGHT_LS
  light.releaseOnWake();
#endif
  light.begin();
  light.setBrightness(25);
  lastActivity = millis();
}
void board_x4_input_ready() {
  events = xQueueCreate(16, sizeof(Event));
  if (events && xTaskCreate(sample, "cw_input", 4096, nullptr, 2, &sampler) == pdPASS) {
    inputReady = true;
  } else {
    Serial.println("FATAL: input sampler allocation failed");
    render_sleep_screen("Input unavailable: restart");
  }
}
bool board_x4_background_wake() { return false; }
void board_x4_resume_standby(bool) {}  // Pro preview pauses voyages during sleep.
uint8_t board_x4_read_buttons() { return held.load(); }
bool board_x4_buffered_input() { return true; }
bool board_x4_button_pending(uint8_t button) { return button == X4_BTN_POWER && powerPending.load(); }
void board_x4_discard_button_events() {
  if (events) xQueueReset(events);
}
int board_x4_take_button_event() {
  if (powerPending.exchange(false)) { lastActivity = millis(); return X4_BTN_POWER; }
  Event event;
  if (!inputReady || xQueueReceive(events, &event, 0) != pdTRUE) return -1;
  lastActivity = millis();
  if (event.kind == Kind::Button) return event.button;
  if (event.kind == Kind::Tap) crosswinds_touch_tap(event.x, event.y);
  else if (event.y < 32 && event.ey - event.y > 70 && abs(event.ex - event.x) < event.ey - event.y)
    light.setBrightness((light.brightness() + 25) % 125);
  else if (event.x < 32 && event.ex - event.x > 70 && abs(event.ey - event.y) < event.ex - event.x)
    return X4_BTN_BACK;
  else crosswinds_touch_swipe(event.x, event.y, event.ex, event.ey);
  return -1;
}
int board_x4_battery_percent() { return batteryPercent; }
bool board_x4_usb_connected() { return charging; }
bool board_x4_asleep() { return false; }
bool board_x4_sleep_soon() { return millis() - lastActivity > IDLE_MS - 60000; }

void board_x4_shutdown() {
  board_x4_sync_active_clock();
  bool saved = false;
  for (int n = 0; n < 3 && !saved; ++n) { saved = save_write(); if (!saved) delay(150); }
  if (!saved) {
    render_sleep_screen("Save failed: staying awake");
    lastActivity = millis();
    render_request_full_refresh();
    return;
  }
  stopping.store(true);
  // Let the sampler finish its current I2C transaction before rails turn off.
  const uint32_t deadline = millis();
  while (sampler && !stopped.load() && millis() - deadline < 1000) delay(15);
  if (sampler && !stopped.load()) {
    stopping.store(false);
    lastActivity = millis();
    Serial.println("Sleep cancelled: input driver did not stop");
    return;
  }
  render_sleep_screen("Press Power to resume");
  panel.deepSleep();
  light.off();
#ifdef FREEINK_FRONTLIGHT_LS
  light.park();
#endif
  freeink::PowerManager::waitForPowerButtonRelease();
  freeink::PowerManager::armPowerButtonWakeup();
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleep();
}
void board_x4_power_off() { board_x4_shutdown(); }
void board_x4_tick() {
  const uint32_t now = millis();
  if (!lastBattery || now - lastBattery >= 30000) {
    lastBattery = now;
    const auto status = battery.readStatus();
    if (status.percentageKnown) batteryPercent = status.percentage;
    charging = (status.externalPowerKnown && status.externalPower) || (status.chargingKnown && status.charging);
    if (status.percentageKnown && batteryPercent <= 2 && !charging) { board_x4_shutdown(); return; }
  }
  if (now - lastActivity >= IDLE_MS) board_x4_shutdown();
  delay(1);
}
#endif
