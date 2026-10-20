// Xteink X4 board layer: power latch, button ladder, battery gauge, USB
// detection, idle timeout and power-off. The desktop preview build (LGFX_SDL)
// emulates the buttons with the keyboard and turns power-off into a sleep
// screen that any key dismisses.

#include "x4.h"
#include "display.h"
#if !defined(LGFX_SDL) || defined(SH_HOST_TESTS)
#include "x4_button_events.h"
#endif

#if defined(LGFX_SDL)
#include <lgfx/v1/platforms/sdl/common.hpp>
#if defined(SH_HOST_TESTS)
extern uint8_t sh_test_buttons();
#endif
#else
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_random.h>
#include <sys/time.h>
#endif

static bool hostAsleep = false;
#if !defined(LGFX_SDL)
// Unlike RTC_DATA_ATTR this survives a watchdog/panic during a wake commit.
// Never trust it after loss of power, brownout, USB flashing or unknown reset.
RTC_NOINIT_ATTR static X4StandbyRecord standbyRecord;
#else
static X4StandbyRecord standbyRecord{};
#endif
static bool standbyClockRetained = false;
static bool standbyTimerWake = false;
static bool standbyFailure = false;

#if SH_X4_STANDBY_DIAGNOSTICS
#if !defined(LGFX_SDL)
RTC_NOINIT_ATTR static X4StandbyDiagnostics standbyDiagnostics;
#else
static X4StandbyDiagnostics standbyDiagnostics{};
#endif
static uint32_t standbyDiagnosticSession = 0;
static uint32_t standbyDiagnosticWake = X4_SD_AWAKE;
static uint32_t standbyDiagnosticReported = 0;
#if !defined(LGFX_SDL)
static void input_diag_report();
#if SH_X4_INPUT_DIAGNOSTICS
static void input_diag_command(int command);
#endif
#endif

static void standby_diag_record(uint32_t phase, int32_t status = 0, uint32_t flags = 0,
                                uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) {
  x4_standby_diagnostic_append(standbyDiagnostics,
    {0, standbyDiagnosticSession, phase, standbyDiagnosticWake, flags, status, a, b, c, d});
}

static void standby_diag_report() {
#if !defined(LGFX_SDL)
  if (standbyTimerWake || !Serial) return;  // never extend a silent timer boot for USB
  // A single read-only retry command makes a missed/partial console capture
  // recoverable after interactive wake without changing the retained history.
  int available = Serial.available();
  while (available-- > 0) {
    int command = Serial.read();
    if (command == 'D') standbyDiagnosticReported = 0;
    else if (command == 'S') input_diag_report();
#if SH_X4_INPUT_DIAGNOSTICS
    else input_diag_command(command);
#endif
  }
#endif
  if (!x4_standby_diagnostics_valid(standbyDiagnostics) || !standbyDiagnostics.count ||
      standbyDiagnosticReported == standbyDiagnostics.nextSequence) return;
  Serial.printf("X4 standby diagnostics v1: count=%lu dropped=%lu (RTC evidence; not a current measurement)\n",
    (unsigned long)standbyDiagnostics.count, (unsigned long)standbyDiagnostics.dropped);
  for (uint32_t i = 0; i < standbyDiagnostics.count; ++i) {
    const auto* event = x4_standby_diagnostic_at(standbyDiagnostics, i);
    Serial.printf("SD seq=%lu session=%lu phase=%lu wake=%lu status=%ld flags=0x%lx a=%lu b=%lu c=%lu d=%lu\n",
      (unsigned long)event->sequence, (unsigned long)event->session, (unsigned long)event->phase,
      (unsigned long)event->wake, (long)event->status, (unsigned long)event->flags,
      (unsigned long)event->a, (unsigned long)event->b, (unsigned long)event->c, (unsigned long)event->d);
  }
  standbyDiagnosticReported = standbyDiagnostics.nextSequence;
}
#else
#define standby_diag_record(...) ((void)0)
#define standby_diag_report() ((void)0)
#endif

static bool save_with_retry() {
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (save_write()) return true;
    delay(150);
  }
  return false;
}

#if SH_X4_STANDBY_DIAGNOSTICS
static bool standby_diag_save(uint32_t phase) {
  bool saved = save_with_retry();
  // This is deliberately AFTER the outer atomic write returns. RAM state
  // from a failed write is labelled failure, never counted as committed.
  standby_diag_record(phase, saved ? 0 : -1, 0, g_campaign.standbyAccountedRealSeconds,
    g_campaign.worldMinutes, g_campaign.deliveredUnits, (uint32_t)g_campaign.coins);
  return saved;
}
#else
#define standby_diag_save(phase) save_with_retry()
#endif

static bool sailing_for_standby() {
  if (g_campaign.paused) return false;
  for (int i = 0; i < g_campaign.shipCount; ++i)
    if (g_ships[i].state == SHIP_SAILING) return true;
  return false;
}

[[maybe_unused]] static uint32_t standby_next_wake() {
  uint32_t seconds = X4_STANDBY_MAX_WAKE_SECONDS;
  uint32_t rate = SIM_WORLD_MINUTES_PER_REAL_SECOND *
                  (g_campaign.timeScale ? g_campaign.timeScale : 1u);
  for (int i = 0; i < g_campaign.shipCount; ++i) {
    const Ship& ship = g_ships[i];
    if (ship.state != SHIP_SAILING) continue;
    uint32_t remaining = ship.legTotalMinutes > ship.legElapsedMinutes ?
                         ship.legTotalMinutes - ship.legElapsedMinutes : 1;
    seconds = min(seconds, x4_standby_wake_seconds(remaining, rate));
  }
  return seconds;
}

static void invalidate_standby() {
  standbyRecord.magic = 0;
  g_campaign.standbySessionId = 0;
  g_campaign.standbyAccountedRealSeconds = 0;
}

static bool abort_standby_save() {
  save_suspend(false);
  // A failed commit must never expose its uncommitted cash/cargo to play.
  // Reload the coherent prior/current snapshot before abandoning its clock.
  if (!save_read()) {
    standby_diag_record(X4_SD_LOAD, -1);
    Serial.println("ERROR: standby recovery could not reload a coherent company");
    standbyFailure = true;
    return false;
  }
  invalidate_standby();
  journal_add(2, "Standby save failed; kept last saved progress.");
  standby_diag_save(X4_SD_ABORT_SAVE);  // best effort: invalid RTC already prevents replay
  standbyFailure = false;
  return false;
}

static bool begin_standby_checkpoint(uint64_t nowUs, uint32_t session) {
  if (!sailing_for_standby() || !nowUs || !session) return false;
#if SH_X4_STANDBY_DIAGNOSTICS
  standbyDiagnosticSession = session;
  standbyDiagnosticWake = X4_SD_AWAKE;
#endif
  g_campaign.standbySessionId = session;
  g_campaign.standbyAccountedRealSeconds = 0;
  if (!standby_diag_save(X4_SD_BEGIN_SAVE)) return abort_standby_save();
  standbyRecord = x4_standby_record(session, g_campaign.seed, nowUs);
  return true;
}

static bool reconcile_standby(uint64_t nowUs, bool retainedClock, bool userWake) {
  X4StandbyDelta delta = x4_standby_delta(standbyRecord, retainedClock,
    g_campaign.standbySessionId, g_campaign.seed,
    g_campaign.standbyAccountedRealSeconds, nowUs);
#if SH_X4_STANDBY_DIAGNOSTICS
  standbyDiagnosticWake = userWake ? X4_SD_INTERACTIVE : X4_SD_TIMER;
#endif
  standby_diag_record(X4_SD_DELTA, delta.trusted ? 0 : -1, delta.trusted ? X4_SD_TRUSTED : 0,
    delta.targetSeconds, delta.pendingSeconds, g_campaign.standbyAccountedRealSeconds, g_campaign.worldMinutes);
  if (!delta.trusted) {
    bool pending = g_campaign.standbySessionId != 0;
    invalidate_standby();
    if (pending) {
      journal_add(2, "Standby clock unavailable; resumed saved progress.");
      if (!standby_diag_save(X4_SD_CLOSE_SAVE)) return abort_standby_save();
    }
    return false;
  }
  uint32_t rate = SIM_WORLD_MINUTES_PER_REAL_SECOND *
                  (g_campaign.timeScale ? g_campaign.timeScale : 1u);
  while (delta.pendingSeconds && sailing_for_standby()) {
    uint32_t seconds = min(delta.pendingSeconds, X4_STANDBY_MAX_WAKE_SECONDS);
    if ((uint64_t)seconds * rate > UINT32_MAX - g_campaign.worldMinutes) {
      invalidate_standby();
      journal_add(2, "Standby clock exceeds world limit; time paused.");
      if (!standby_diag_save(X4_SD_CLOSE_SAVE)) return abort_standby_save();
      return false;
    }
    // Any sim-internal save sees a deferred success. The actual snapshot
    // commits the clock watermark and every settlement together below.
    save_suspend(true);
    g_campaign.standbyAccountedRealSeconds += seconds;
    sim_advance_minutes(seconds * rate);
    save_suspend(false);
    if (!standby_diag_save(X4_SD_ADVANCE_SAVE)) return abort_standby_save();
    delta.pendingSeconds -= seconds;
  }
  if (userWake || !sailing_for_standby()) {
    // Arrived ships and legacy decisions wait safely; no timer redispatches.
    g_campaign.standbySessionId = 0;
    g_campaign.standbyAccountedRealSeconds = 0;
    if (!standby_diag_save(X4_SD_CLOSE_SAVE)) return abort_standby_save();
    if (userWake) standbyRecord.magic = 0;
  }
  return true;
}

#if defined(SH_HOST_TESTS)
bool board_x4_test_standby_begin(uint64_t nowUs, uint32_t session) {
  return begin_standby_checkpoint(nowUs, session);
}
bool board_x4_test_standby_resume(uint64_t nowUs, bool retainedClock, bool userWake) {
  return reconcile_standby(nowUs, retainedClock, userWake);
}
void board_x4_test_standby_lose_clock() { standbyRecord.magic = 0; }
#if SH_X4_STANDBY_DIAGNOSTICS
const X4StandbyDiagnostics& board_x4_test_standby_diagnostics() { return standbyDiagnostics; }
void board_x4_test_standby_diagnostics_reset() { x4_standby_diagnostics_reset(standbyDiagnostics); }
#endif
#endif

// Idle limits: a docked fleet sleeps sooner than one with ships at sea, so a
// long voyage can be left to run.
static constexpr uint32_t IDLE_SLEEP_DOCKED_MS = 15u * 60u * 1000u;
static constexpr uint32_t IDLE_SLEEP_SAILING_MS = 3u * 60u * 60u * 1000u;

static bool fleet_at_sea() {
  for (int i = 0; i < g_campaign.shipCount; ++i)
    if (g_ships[i].state == SHIP_SAILING || g_ships[i].state == SHIP_AWAITING_DECISION)
      return true;
  return false;
}

#if !defined(LGFX_SDL)
static int batteryPercent = -1;
static uint32_t batteryReadAt = 0;
static X4PowerWakeGuard powerWakeGuard;
static X4ButtonEvents buttonEvents;
static portMUX_TYPE buttonEventsMux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t adcMutexStorage;
static SemaphoreHandle_t adcMutex = nullptr;
static StaticTask_t buttonTaskStorage;
static constexpr uint32_t BUTTON_TASK_STACK_BYTES = 3072;
static StackType_t buttonTaskStack[BUTTON_TASK_STACK_BYTES / sizeof(StackType_t)];
static TaskHandle_t buttonTask = nullptr;
static uint32_t inputSampleCount = 0, inputSampleAt = 0, inputEventsTaken = 0;
static uint16_t inputFrontRaw = 0, inputSideRaw = 0;
static uint8_t inputSampleMask = 0;
#if SH_X4_INPUT_DIAGNOSTICS
// This opt-in hardware QA build records brief physical gestures even when
// USB is read after the gesture or while the panel is busy. Serial navigation
// is separate from these physical counters and never synthesizes Power.
static uint32_t inputPhysicalEdges[X4_BTN_COUNT]{};
static uint32_t inputPhysicalAt[X4_BTN_COUNT]{};
static uint16_t inputFrontMin = 4095, inputSideMin = 4095;
static int inputDiagnosticButton = -1;
static uint32_t inputDiagnosticTaken = 0;
#endif
static_assert(X4_BTN_COUNT == X4ButtonEvents::BUTTON_COUNT &&
              X4_BTN_POWER == X4ButtonEvents::POWER_BUTTON, "button sampler mapping");

static void sample_buttons(void*) {
  TickType_t previous = xTaskGetTickCount();
  for (;;) {
    uint8_t mask = board_x4_read_buttons();
    uint32_t now = millis();
    portENTER_CRITICAL(&buttonEventsMux);
#if SH_X4_INPUT_DIAGNOSTICS
    for (uint8_t b = 0; b < X4_BTN_COUNT; ++b) {
      if ((mask & ~inputSampleMask) & (1u << b)) {
        ++inputPhysicalEdges[b]; inputPhysicalAt[b] = now;
      }
    }
#endif
    ++inputSampleCount; inputSampleAt = now; inputSampleMask = mask;
    buttonEvents.sample(now, mask);
    portEXIT_CRITICAL(&buttonEventsMux);
    vTaskDelayUntil(&previous, pdMS_TO_TICKS(10));
  }
}

bool board_x4_buffered_input() { return buttonTask != nullptr; }

bool board_x4_button_pending(uint8_t button) {
  portENTER_CRITICAL(&buttonEventsMux);
  bool pending = buttonEvents.pending(button);
  portEXIT_CRITICAL(&buttonEventsMux);
  return pending;
}

int board_x4_take_button_event() {
  portENTER_CRITICAL(&buttonEventsMux);
  int event = buttonEvents.take();
  if (event >= 0) ++inputEventsTaken;
#if SH_X4_INPUT_DIAGNOSTICS
  else if (inputDiagnosticButton >= 0) {
    event = inputDiagnosticButton; inputDiagnosticButton = -1;
    ++inputDiagnosticTaken;
  }
#endif
  portEXIT_CRITICAL(&buttonEventsMux);
  return event;
}

void board_x4_discard_button_events() {
  portENTER_CRITICAL(&buttonEventsMux);
  buttonEvents.discardOrdinary();
  portEXIT_CRITICAL(&buttonEventsMux);
}

static int battery_percent_from_mv(int mv) {
  static const int16_t curve[][2] = {
    { 4200, 100 }, { 4100, 92 }, { 4000, 82 }, { 3900, 68 }, { 3800, 50 },
    { 3700, 30 }, { 3600, 12 }, { 3500, 4 }, { 3400, 0 }
  };
  const int count = (int)(sizeof(curve) / sizeof(curve[0]));
  if (mv >= curve[0][0]) return 100;
  for (int i = 1; i < count; ++i) {
    if (mv >= curve[i][0]) {
      int span = curve[i - 1][0] - curve[i][0];
      int t = mv - curve[i][0];
      return curve[i][1] + (curve[i - 1][1] - curve[i][1]) * t / span;
    }
  }
  return 0;
}

// 12-bit analogRead() ranges from the FreeInk SDK InputManager, midpoints
// between measured levels: BACK ~3512, CONFIRM ~2694, LEFT ~1493, RIGHT ~5 on
// GPIO1; UP ~2242, DOWN ~5 on GPIO2. An idle ladder reads near 4095.
static int decode_front(int raw) {
  if (raw > 3800) return -1;
  if (raw > 3100) return X4_BTN_BACK;
  if (raw > 2090) return X4_BTN_CONFIRM;
  if (raw > 750) return X4_BTN_LEFT;
  return X4_BTN_RIGHT;
}

static int decode_side(int raw) {
  if (raw > 3300) return -1;
  if (raw > 1120) return X4_BTN_UP;
  return X4_BTN_DOWN;
}

static uint64_t standby_now_us() {
  timeval value{};
  if (gettimeofday(&value, nullptr) != 0 || value.tv_sec < 0) return 0;
  return (uint64_t)value.tv_sec * 1000000u + value.tv_usec;
}

static bool retained_clock_reset(esp_reset_reason_t reason) {
  return reason == ESP_RST_DEEPSLEEP || reason == ESP_RST_SW || reason == ESP_RST_PANIC ||
         reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

static void arm_wake_and_sleep() {
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  pinMode(X4_PIN_BTN_POWER, INPUT_PULLUP);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << X4_PIN_BTN_POWER, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_sleep_config_gpio_isolate();
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_start();
}

static void enter_standby_sleep(bool keepTimer) {
  // Keep the battery latch and SD chip-select asserted while digital power
  // is down. Wake config deliberately avoids isolating these held outputs.
  if (buttonTask) vTaskSuspend(buttonTask);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  pinMode(X4_PIN_BTN_POWER, INPUT_PULLUP);
  esp_err_t configured = esp_deep_sleep_enable_gpio_wakeup(
    1ULL << X4_PIN_BTN_POWER, ESP_GPIO_WAKEUP_GPIO_LOW);
#if SH_X4_STANDBY_DIAGNOSTICS
  uint32_t evidence = configured == ESP_OK ? X4_SD_GPIO_ARMED : 0;
#endif
  if (configured == ESP_OK && keepTimer)
    configured = esp_sleep_enable_timer_wakeup((uint64_t)standby_next_wake() * 1000000u);
#if SH_X4_STANDBY_DIAGNOSTICS
  if (keepTimer && configured == ESP_OK) evidence |= X4_SD_TIMER_ARMED;
#endif
  digitalWrite(X4_PIN_POWER_LATCH, HIGH);
  digitalWrite(X4_PIN_SD_CS, HIGH);
  if (configured == ESP_OK) configured = gpio_hold_en((gpio_num_t)X4_PIN_POWER_LATCH);
#if SH_X4_STANDBY_DIAGNOSTICS
  if (configured == ESP_OK) evidence |= X4_SD_LATCH_HELD;
#endif
  if (configured == ESP_OK) configured = gpio_hold_en((gpio_num_t)X4_PIN_SD_CS);
#if SH_X4_STANDBY_DIAGNOSTICS
  if (configured == ESP_OK) evidence |= X4_SD_SD_HELD;
  if (digitalRead(X4_PIN_POWER_LATCH) == HIGH) evidence |= X4_SD_LATCH_HIGH;
  if (digitalRead(X4_PIN_SD_CS) == HIGH) evidence |= X4_SD_SD_HIGH;
  if (board_x4_usb_connected()) evidence |= X4_SD_USB_PRESENT;
  standby_diag_record(X4_SD_SLEEP_SETUP, configured, evidence, keepTimer ? standby_next_wake() : 0,
    g_campaign.standbyAccountedRealSeconds, g_campaign.worldMinutes);
#endif
  if (configured == ESP_OK) {
    gpio_deep_sleep_hold_en();
    configured = esp_deep_sleep_try_to_start();
  }
  standby_diag_record(X4_SD_SLEEP_REJECT, configured);
  // Only a rejected/configuration-failed sleep returns. Restore normal GPIO
  // control before resuming the input task and interactive game.
  gpio_hold_dis((gpio_num_t)X4_PIN_POWER_LATCH);
  gpio_hold_dis((gpio_num_t)X4_PIN_SD_CS);
  gpio_deep_sleep_hold_dis();
  if (buttonTask) vTaskResume(buttonTask);
  Serial.printf("ERROR: standby sleep rejected (%d)\n", (int)configured);
  journal_add(2, "Standby unavailable; reader stayed on.");
}

bool board_x4_background_wake() { return standbyTimerWake; }

void board_x4_resume_standby(bool loaded) {
  standby_diag_record(X4_SD_LOAD, loaded ? 0 : -1, 0, g_campaign.standbySessionId,
    g_campaign.standbyAccountedRealSeconds, g_campaign.worldMinutes, g_campaign.deliveredUnits);
  if (!loaded) { standbyRecord.magic = 0; standbyTimerWake = false; return; }
  bool pending = g_campaign.standbySessionId != 0;
  if (!pending) { standbyRecord.magic = 0; standbyTimerWake = false; return; }
  bool reconciled = reconcile_standby(standby_now_us(),
    SH_X4_VOYAGE_STANDBY && standbyClockRetained, !standbyTimerWake);
  if (standbyFailure) { board_x4_shutdown(); return; }
  if (standbyTimerWake && reconciled) {
    if (!board_x4_usb_connected() && board_x4_battery_percent() <= X4_BATTERY_OFF_PERCENT) {
      board_x4_shutdown(); return;
    }
    enter_standby_sleep(sailing_for_standby());
    // Rejected sleep returns to an interactive boot; its epoch must close
    // before the normal awake clock starts counting the same seconds.
    reconcile_standby(standby_now_us(), true, true);
    if (standbyFailure) { board_x4_shutdown(); return; }
  }
  standbyTimerWake = false;
}

void board_x4_begin_early() {
  // The power button energizes the battery rail only while held; latching
  // GPIO13 high keeps the board alive after release. Do this before anything
  // slow. Also park the SD card's chip select so it never answers on the
  // display's SPI bus.
  pinMode(X4_PIN_POWER_LATCH, OUTPUT);
  digitalWrite(X4_PIN_POWER_LATCH, HIGH);
  pinMode(X4_PIN_SD_CS, OUTPUT);
  digitalWrite(X4_PIN_SD_CS, HIGH);
  // Restore the output register before releasing deep-sleep holds, avoiding
  // a low glitch that would cut the very rail used for timer wake.
  gpio_hold_dis((gpio_num_t)X4_PIN_POWER_LATCH);
  gpio_hold_dis((gpio_num_t)X4_PIN_SD_CS);
  gpio_deep_sleep_hold_dis();
  pinMode(X4_PIN_BTN_POWER, INPUT_PULLUP);
  pinMode(X4_PIN_USB_DETECT, INPUT);
  pinMode(X4_PIN_BATTERY, INPUT);
  analogReadResolution(12);
  standbyClockRetained = retained_clock_reset(esp_reset_reason()) && x4_standby_valid(standbyRecord);
  standbyTimerWake = SH_X4_VOYAGE_STANDBY && standbyClockRetained &&
                     esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
#if SH_X4_STANDBY_DIAGNOSTICS
  bool priorEvidence = x4_standby_diagnostics_valid(standbyDiagnostics) &&
    esp_reset_reason() != ESP_RST_POWERON && esp_reset_reason() != ESP_RST_BROWNOUT;
  if (!priorEvidence) x4_standby_diagnostics_reset(standbyDiagnostics);
  // Diagnostic history may survive a USB/software reset to explain it. That
  // never makes such a reset's elapsed clock trusted by the game.
  standbyDiagnosticSession = x4_standby_valid(standbyRecord) ? standbyRecord.session : 0;
  standbyDiagnosticWake = standbyTimerWake ? X4_SD_TIMER :
    esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO ? X4_SD_INTERACTIVE : X4_SD_RESET;
  standby_diag_record(X4_SD_BOOT, priorEvidence ? 0 : 1, standbyClockRetained ? X4_SD_TRUSTED : 0,
    (uint32_t)esp_reset_reason(), (uint32_t)esp_sleep_get_wakeup_cause());
#endif

  // Once hardware has woken, finish startup even if Power is already released.
  // The wake gesture is absorbed until the first screen is ready; rejecting a
  // short wake here would silently leave the old e-paper image frozen in place.
}

void board_x4_begin() {
  adcMutex = xSemaphoreCreateMutexStatic(&adcMutexStorage);
  if (adcMutex) buttonTask = xTaskCreateStatic(sample_buttons, "ships-buttons",
    BUTTON_TASK_STACK_BYTES, nullptr, 2, buttonTaskStack, &buttonTaskStorage);
  if (!buttonTask) Serial.println("ERROR: X4 button sampler unavailable; using polling fallback");
  else Serial.println("X4 input: buffered sampler ready (10ms / 20ms debounce)");
}

void board_x4_input_ready() {
  portENTER_CRITICAL(&buttonEventsMux);
  powerWakeGuard.beginInteractive();
  portEXIT_CRITICAL(&buttonEventsMux);
  Serial.println("X4 input: ready; wake Power gesture suppressed until stable release");
}

uint8_t board_x4_read_buttons() {
  uint8_t mask = 0;
  // Battery measurements use this same ADC peripheral on the main task.
  if (adcMutex) xSemaphoreTake(adcMutex, portMAX_DELAY);
  uint16_t frontRaw = analogRead(X4_PIN_BTN_ADC1);
  int front = decode_front(frontRaw);
  if (front >= 0) mask |= (uint8_t)(1u << front);
  uint16_t sideRaw = analogRead(X4_PIN_BTN_ADC2);
  int side = decode_side(sideRaw);
  if (side >= 0) mask |= (uint8_t)(1u << side);
  if (adcMutex) xSemaphoreGive(adcMutex);
  bool power = digitalRead(X4_PIN_BTN_POWER) == LOW;
  portENTER_CRITICAL(&buttonEventsMux);
  inputFrontRaw = frontRaw; inputSideRaw = sideRaw;
#if SH_X4_INPUT_DIAGNOSTICS
  inputFrontMin = min(inputFrontMin, frontRaw);
  inputSideMin = min(inputSideMin, sideRaw);
#endif
  bool acceptedPower = powerWakeGuard.sample(millis(), power);
  portEXIT_CRITICAL(&buttonEventsMux);
  if (acceptedPower) mask |= (uint8_t)(1u << X4_BTN_POWER);
  return mask;
}

#if SH_X4_STANDBY_DIAGNOSTICS
static void input_diag_report() {
  portENTER_CRITICAL(&buttonEventsMux);
  uint32_t samples = inputSampleCount, at = inputSampleAt, taken = inputEventsTaken;
  uint16_t front = inputFrontRaw, side = inputSideRaw;
  uint8_t mask = inputSampleMask;
  uint32_t dropped = buttonEvents.dropped();
  portEXIT_CRITICAL(&buttonEventsMux);
  Serial.printf("X4 live: ms=%lu samples=%lu age=%lu raw=%u/%u mask=%u events=%lu presses=%lu dropped=%lu title=%u scene=%u card=%u ship=%u port=%u dirty=%u loop_stack=%u button_stack=%u heap=%lu\n",
    (unsigned long)millis(), (unsigned long)samples, (unsigned long)(millis() - at),
    front, side, mask, (unsigned long)taken, (unsigned long)x4_press_count(), (unsigned long)dropped,
    g_ui.title, g_ui.scene, g_ui.card, g_ui.selectedShip, g_ui.selectedPort, g_ui.dirty,
    (unsigned)uxTaskGetStackHighWaterMark(nullptr),
    buttonTask ? (unsigned)uxTaskGetStackHighWaterMark(buttonTask) : 0u,
    (unsigned long)esp_get_free_heap_size());
#if SH_X4_INPUT_DIAGNOSTICS
  uint32_t edges[X4_BTN_COUNT], atMs[X4_BTN_COUNT];
  portENTER_CRITICAL(&buttonEventsMux);
  memcpy(edges, inputPhysicalEdges, sizeof(edges));
  memcpy(atMs, inputPhysicalAt, sizeof(atMs));
  uint16_t frontMin = inputFrontMin, sideMin = inputSideMin;
  uint32_t injected = inputDiagnosticTaken;
  portEXIT_CRITICAL(&buttonEventsMux);
  Serial.printf("X4 physical: min=%u/%u edges=", frontMin, sideMin);
  for (int b = 0; b < X4_BTN_COUNT; ++b)
    Serial.printf("%s%lu@%lu", b ? "," : "", (unsigned long)edges[b], (unsigned long)atMs[b]);
  HitRegion focus{}; bool focused = x4_focus_region(&focus);
  Serial.printf(" injected=%lu focus=%d,%d,%d,%d coins=%ld\n", (unsigned long)injected,
    focused ? focus.x : -1, focused ? focus.y : -1, focus.w, focus.h, (long)g_campaign.coins);
#endif
}
#if SH_X4_INPUT_DIAGNOSTICS
static void input_diag_command(int command) {
  if (command == 'F') {
    // Export the composed monochrome image without stopping either CPU task.
    // PBM uses one for black, opposite to the controller's white-is-one RAM.
    Serial.printf("X4 framebuffer\nP4\n%d %d\n", IW, IH);
    uint8_t row[(IW + 7) / 8];
    for (int y = 0; y < IH; ++y) {
      memset(row, 0, sizeof(row));
      for (int x = 0; x < IW; ++x)
        if (display.readPixel(x, y) == 0) row[x / 8] |= 0x80u >> (x % 8);
      Serial.write(row, sizeof(row));
      delay(1);
    }
    Serial.println("X4 framebuffer done");
    return;
  }
  if (command < '0' || command > '5') return;
  portENTER_CRITICAL(&buttonEventsMux);
  bool accepted = inputDiagnosticButton < 0;
  if (accepted) inputDiagnosticButton = command - '0';
  portEXIT_CRITICAL(&buttonEventsMux);
  Serial.printf("X4 diagnostic key: %c %s\n", command, accepted ? "queued" : "busy");
}
#endif
#endif

int board_x4_battery_percent() {
  uint32_t now = millis();
  if (batteryPercent < 0 || now - batteryReadAt >= 30000) {
    uint32_t sum = 0;
    if (adcMutex) xSemaphoreTake(adcMutex, portMAX_DELAY);
    for (int i = 0; i < 8; ++i) sum += analogReadMilliVolts(X4_PIN_BATTERY);
    if (adcMutex) xSemaphoreGive(adcMutex);
    batteryPercent = battery_percent_from_mv((int)(sum / 8u) * 2);
    batteryReadAt = now;
  }
  return batteryPercent;
}

bool board_x4_usb_connected() { return digitalRead(X4_PIN_USB_DETECT) == HIGH; }

void board_x4_shutdown() {
  // One refresh only: at a brownout threshold the rail may not survive two.
  if (!standbyFailure) board_x4_sync_active_clock();
  invalidate_standby();
  // A failed coherent reload leaves untrusted RAM. Preserve its disk records
  // for recovery rather than overwriting them with a partially loaded company.
  bool saved = !standbyFailure && save_with_retry();
  standby_diag_record(X4_SD_SHUTDOWN_SAVE, saved ? 0 : -1, 0, g_campaign.standbyAccountedRealSeconds,
    g_campaign.worldMinutes, g_campaign.deliveredUnits, (uint32_t)g_campaign.coins);
  if (!saved) Serial.println("save: final write failed");
  if (!standbyTimerWake) render_sleep_screen(saved ? "SHUTDOWN" : "SAVE FAILED");
  if (!saved) delay(2000);  // let the failure be seen before the panel sleeps
  bool critical = !board_x4_usb_connected() && board_x4_battery_percent() >= 0 &&
                  board_x4_battery_percent() <= X4_BATTERY_OFF_PERCENT;
  if (!saved && !critical && !standbyFailure) {
    journal_add(2, "Save failed; reader stayed on to preserve progress.");
    render_request_full_refresh();
    return;
  }
  if (!standbyTimerWake) display.sleep();
  // Wait for the power button to be released so it cannot wake us at once.
  uint32_t start = millis();
  while (digitalRead(X4_PIN_BTN_POWER) == LOW && millis() - start < 5000) delay(20);
  // Dropping the latch cuts the battery rail (a real power-off). On USB power
  // the board stays up, so also enter deep sleep with the power button armed.
  gpio_hold_dis((gpio_num_t)X4_PIN_POWER_LATCH);
  pinMode(X4_PIN_POWER_LATCH, OUTPUT);
  digitalWrite(X4_PIN_POWER_LATCH, LOW);
  gpio_hold_en((gpio_num_t)X4_PIN_POWER_LATCH);
  arm_wake_and_sleep();
}

void board_x4_power_off() {
  bool eligible = SH_X4_VOYAGE_STANDBY && sailing_for_standby() &&
                  (board_x4_usb_connected() || board_x4_battery_percent() > X4_BATTERY_OFF_PERCENT);
  if (!eligible) { board_x4_shutdown(); return; }
  uint32_t pressedAt = millis();
  while (digitalRead(X4_PIN_BTN_POWER) == LOW && millis() - pressedAt < 2000) delay(10);
  if (digitalRead(X4_PIN_BTN_POWER) == LOW) { board_x4_shutdown(); return; }
  render_sleep_screen("STANDBY");
  board_x4_sync_active_clock();
  uint32_t session = esp_random();
  if (!session) session = 1;
  if (!begin_standby_checkpoint(standby_now_us(), session)) {
    if (standbyFailure) { board_x4_shutdown(); return; }
    render_request_full_refresh();
    return;
  }
  display.sleep();
  enter_standby_sleep(true);
  // A rejected deep sleep cannot leave an active epoch while normal play runs.
  reconcile_standby(standby_now_us(), true, true);
  display.wakeup();
  render_request_full_refresh();
}

bool board_x4_asleep() { return false; }

#else  // LGFX_SDL desktop preview

// LovyanGFX's SDL panel maps the arrow keys to virtual GPIOs 36..39 by
// default; main.cpp adds Return/Space, Backspace/Escape and P.
static constexpr uint8_t HOST_GPIO_UP = 36;
static constexpr uint8_t HOST_GPIO_RIGHT = 37;
static constexpr uint8_t HOST_GPIO_DOWN = 38;
static constexpr uint8_t HOST_GPIO_LEFT = 39;
static constexpr uint8_t HOST_GPIO_CONFIRM = 40;
static constexpr uint8_t HOST_GPIO_BACK = 41;
static constexpr uint8_t HOST_GPIO_POWER = 42;

void board_x4_begin_early() {}
void board_x4_begin() {}
void board_x4_input_ready() {}
bool board_x4_background_wake() { return standbyTimerWake; }
void board_x4_resume_standby(bool loaded) {
  if (!loaded) { standbyRecord.magic = 0; return; }
  if (g_campaign.standbySessionId)
    reconcile_standby((uint64_t)millis() * 1000u, standbyClockRetained, true);
}
#if defined(SH_HOST_TESTS)
static bool hostBufferedInput = false;
static X4ButtonEvents hostButtonEvents;
void board_x4_test_buffered_input(bool enabled) {
  hostBufferedInput = enabled;
  hostButtonEvents = X4ButtonEvents{};
}
void board_x4_test_sample_buttons(uint32_t now, uint8_t mask) {
  hostButtonEvents.sample(now, mask);
}
bool board_x4_buffered_input() { return hostBufferedInput; }
int board_x4_take_button_event() { return hostButtonEvents.take(); }
bool board_x4_button_pending(uint8_t button) { return hostButtonEvents.pending(button); }
void board_x4_discard_button_events() { hostButtonEvents.discardOrdinary(); }
#else
bool board_x4_buffered_input() { return false; }
int board_x4_take_button_event() { return -1; }
bool board_x4_button_pending(uint8_t) { return false; }
void board_x4_discard_button_events() {}
#endif

uint8_t board_x4_read_buttons() {
  uint8_t mask = 0;
  if (!lgfx::gpio_in(HOST_GPIO_UP)) mask |= 1u << X4_BTN_UP;
  if (!lgfx::gpio_in(HOST_GPIO_DOWN)) mask |= 1u << X4_BTN_DOWN;
  if (!lgfx::gpio_in(HOST_GPIO_LEFT)) mask |= 1u << X4_BTN_LEFT;
  if (!lgfx::gpio_in(HOST_GPIO_RIGHT)) mask |= 1u << X4_BTN_RIGHT;
  if (!lgfx::gpio_in(HOST_GPIO_CONFIRM)) mask |= 1u << X4_BTN_CONFIRM;
  if (!lgfx::gpio_in(HOST_GPIO_BACK)) mask |= 1u << X4_BTN_BACK;
  if (!lgfx::gpio_in(HOST_GPIO_POWER)) mask |= 1u << X4_BTN_POWER;
#if defined(SH_HOST_TESTS)
  mask |= sh_test_buttons();
#endif
  return mask;
}

int board_x4_battery_percent() {
  const char* fixed = getenv("SH_BATTERY");
  return fixed ? atoi(fixed) : 87;
}

bool board_x4_usb_connected() { return getenv("SH_USB") != nullptr; }

void board_x4_shutdown() {
  if (!standbyFailure) board_x4_sync_active_clock();
  invalidate_standby();
  bool saved = false;
  for (int attempt = 0; attempt < 3 && !saved && !standbyFailure; ++attempt) saved = save_write();
  render_sleep_screen(saved ? "SHUTDOWN" : "SAVE FAILED");
  bool critical = !board_x4_usb_connected() && board_x4_battery_percent() >= 0 &&
                  board_x4_battery_percent() <= X4_BATTERY_OFF_PERCENT;
  if (!saved && !critical && !standbyFailure) {
    journal_add(2, "Save failed; reader stayed on to preserve progress.");
    render_request_full_refresh();
    return;
  }
  hostAsleep = true;
}

void board_x4_power_off() {
  if (!SH_X4_VOYAGE_STANDBY || !sailing_for_standby()) { board_x4_shutdown(); return; }
  render_sleep_screen("STANDBY");
  board_x4_sync_active_clock();
  if (!begin_standby_checkpoint((uint64_t)millis() * 1000u, (uint32_t)millis() | 1u)) return;
  standbyClockRetained = true;
  hostAsleep = true;
}

bool board_x4_asleep() { return hostAsleep; }

// Called by the input layer when a key arrives while the sleep screen shows.
void board_x4_host_wake() {
  hostAsleep = false;
  if (g_campaign.standbySessionId)
    reconcile_standby((uint64_t)millis() * 1000u, standbyClockRetained, true);
  x4_prepare_return(true);
  render_request_full_refresh();
}

#endif

static bool sleepSoon = false;
bool board_x4_sleep_soon() { return sleepSoon; }

void board_x4_tick() {
  if (board_x4_asleep()) return;
  standby_diag_report();
#if !defined(LGFX_SDL)
  static bool inputFailureReported = false;
  if (!buttonTask && !inputFailureReported) {
    // The game is initialized now, so a failed sampler also reaches the
    // visible ticker/journal rather than living only on the USB console.
    inputFailureReported = true;
    journal_add(2, "Input sampler failed; hold keys until screen updates.");
  }
#endif
  // Low battery: one warning per episode, then a save and power-off before
  // the cell browns out mid-write. USB power cancels both.
  int percent = board_x4_battery_percent();
  bool usb = board_x4_usb_connected();
  static bool warned = false;
  if (usb || percent > X4_BATTERY_WARN_PERCENT + 5) warned = false;
  else if (!warned && percent >= 0 && percent <= X4_BATTERY_WARN_PERCENT) {
    warned = true;
    sim_set_notice(SIM_LOW_BATTERY);
  }
  if (!usb && percent >= 0 && percent <= X4_BATTERY_OFF_PERCENT) { board_x4_shutdown(); return; }
  // A paused game is not waiting on anything at sea: use the short allowance.
  uint32_t idle = millis() - x4_last_input_ms();
  uint32_t limit = fleet_at_sea() && !g_campaign.paused ? IDLE_SLEEP_SAILING_MS : IDLE_SLEEP_DOCKED_MS;
  sleepSoon = idle + 60000u >= limit;  // the top bar warns before the screen flashes to sleep
  if (idle >= limit) board_x4_power_off();
}

