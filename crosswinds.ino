// Crosswinds: a maritime trading game for the Xteink X4 e-paper reader (ESP32-C3).
//
// Build with `make device-x4` and flash with `make upload-x4 PORT=...`. They pass the
// board options the X4 needs; see README.md and docs/XTEINK_X4.md.
//
// Required library: LovyanGFX 1.2.24.
#include "game.h"
#include "display.h"
#include <esp_random.h>
#include "x4.h"

#define WIPE_SAVE 0

static uint32_t lastInput = 0;
static uint32_t lastSimulation = 0;
static uint32_t lastAutosave = 0;
static bool activeClockReady = false;

static void advance_live_clock(uint32_t now) {
  if (!activeClockReady || now - lastSimulation < 1000) return;
  uint32_t elapsedSeconds = (now - lastSimulation) / 1000;
  if (elapsedSeconds > 60) elapsedSeconds = 60;
  uint32_t scale = g_campaign.timeScale ? g_campaign.timeScale : 1;
#ifdef SH_HOST_TESTS
  if (const char* s = getenv("SH_TIME_SCALE")) {
    long parsed = atol(s);
    if (parsed > 0 && parsed <= 1000) scale = (uint32_t)parsed;
  }
#endif
  uint8_t oldStates[MAX_SHIPS];
  uint8_t oldShipCount = g_campaign.shipCount;
  for (int i = 0; i < oldShipCount; ++i) oldStates[i] = g_ships[i].state;
  sim_advance_minutes(elapsedSeconds * SIM_WORLD_MINUTES_PER_REAL_SECOND * scale);
  bool stateChanged = oldShipCount != g_campaign.shipCount;
  for (int i = 0; i < oldShipCount && !stateChanged; ++i)
    stateChanged = oldStates[i] != g_ships[i].state;
  render_mark_dirty(0, 0, IW, stateChanged ? IH : ACTIONBAR_Y);
  lastSimulation += elapsedSeconds * 1000;
}

void board_x4_sync_active_clock() {
  if (!board_x4_asleep()) advance_live_clock(millis());
}

bool g_bootResumed = false;

void setup() {
  board_x4_begin_early();  // latch battery power before anything slow
  bool backgroundWake = board_x4_background_wake();
  Serial.begin(115200);
  // With no host reading USB, each print would block up to 2 s and freeze the buttons.
  Serial.setTxTimeoutMs(1);
  delay(60);
  Serial.println("\nCrosswinds - booting");
  Serial.println("Crosswinds UI: Merchant Trade 1");

  if (!backgroundWake) {
    if (!display_begin()) Serial.println("FATAL: framebuffer allocation failed");
    board_x4_begin();
  }
  audio_begin();
  if (!save_begin()) Serial.println("WARN: persistence unavailable");

#if WIPE_SAVE
  save_reset();
#endif

  if (save_exists() && save_read()) {
    g_bootResumed = true;
    Serial.println("Resumed company.");
  } else {
    // A save that exists but cannot be read is set aside, not overwritten.
    bool unreadable = save_exists();
    if (unreadable) { save_quarantine(); Serial.println("save: unreadable; set aside as .orphan"); }
    uint32_t seed = esp_random();
    sim_new_campaign(seed ? seed : 0x51A17EEDu);
    if (unreadable) journal_add(2, "Save could not be read; started a new company.");
    save_write();
    Serial.printf("New company, seed %08lx\n", (unsigned long)seed);
  }

  // Timer wakes reconcile a coherent snapshot without waking the display.
  // On a user wake (or a refused standby), normal startup still refreshes the title and first menu in full.
  board_x4_resume_standby(g_bootResumed);
  if (backgroundWake) {
    if (!display_begin()) Serial.println("FATAL: framebuffer allocation failed");
    board_x4_begin();
  }
  x4_prepare_return(g_bootResumed, true, true);
  lastInput = lastSimulation = lastAutosave = millis();
  activeClockReady = true;
  render_frame();
  x4_input_ready();
  board_x4_input_ready();
}

void loop() {
  uint32_t now = millis();
  if (now - lastInput >= 33) {
    input_update();
    now = millis();  // POWER may have settled time while drawing its sleep screen
    lastInput = now;
  }

  if (board_x4_asleep()) lastSimulation = now;  // host sleep reconciles once on wake
  else
    advance_live_clock(now);

  if (now - lastAutosave >= 60000) {
    save_write();
    lastAutosave = now;
  }

  audio_tick();
  board_x4_tick();
  render_tick();
  if (g_ui.dirty) render_frame();
#ifdef SH_HOST_TESTS
  delay(1);
#endif
}
