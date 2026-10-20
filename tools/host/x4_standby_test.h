// Actual board reconciliation, simulation, and coherent snapshot integration.
// Hardware elapsed/current measurements are deliberately outside this test.
#include <LittleFS.h>
#include <unistd.h>
#if SH_X4_STANDBY_DIAGNOSTICS
// Test-only board hooks stay local to this fixture, outside the shipping API.
extern const X4StandbyDiagnostics& board_x4_test_standby_diagnostics();
extern void board_x4_test_standby_diagnostics_reset();
#endif

static bool run_x4_standby_test() {
  int checks = 0;
  bool passed = true;
  auto check = [&](bool ok, const char* message) {
    ++checks;
    if (!ok) { passed = false; ::printf("standby integration: FAIL %s\n", message); }
    return ok;
  };
  constexpr uint64_t epoch = 1000000000u;
  int32_t cashAtSail = 0, arrivalCash = 0;
  uint32_t duration = 0;
#if SH_X4_STANDBY_DIAGNOSTICS
  auto diagnostic = [&](uint32_t phase, uint32_t wake, int32_t status) {
    const auto& history = board_x4_test_standby_diagnostics();
    const X4StandbyDiagnosticEvent* found = nullptr;
    for (uint32_t i = 0; i < history.count; ++i) {
      const auto* event = x4_standby_diagnostic_at(history, i);
      if (event && event->phase == phase && event->wake == wake && event->status == status) found = event;
    }
    return found;
  };
#endif
  auto begin = [&]() {
    save_reset(); board_x4_test_standby_lose_clock();
#if SH_X4_STANDBY_DIAGNOSTICS
    board_x4_test_standby_diagnostics_reset();
#endif
    historical_campaign(42, 1); g_ui.title = 0;
    VoyageQuote quote = sim_voyage_offer(0, 1, VOYAGE_FREIGHT, GOOD_TEXTILES, 4);
    if (!check(quote.code == SIM_OK, "opening freight quote is usable") ||
        !check(sim_voyage_dispatch(0, quote), "booked ship dispatched")) return false;
    cashAtSail = g_campaign.coins; arrivalCash = quote.arrivalCoins;
    uint32_t rate = SIM_WORLD_MINUTES_PER_REAL_SECOND * g_campaign.timeScale;
    duration = quote.minutes / rate + (quote.minutes % rate != 0);
    return check(board_x4_test_standby_begin(epoch, 1234), "standby checkpoint persisted before sleep");
  };
  if (!begin()) return false;
#if SH_X4_STANDBY_DIAGNOSTICS
  check(diagnostic(X4_SD_BEGIN_SAVE, X4_SD_AWAKE, 0) != nullptr,
        "diagnostics record the successful persisted session start");
#endif
  uint32_t half = duration / 2;
  check(board_x4_test_standby_resume(epoch + (uint64_t)half * 1000000u, true, false),
        "timer wake reconciles partial voyage");
  uint16_t partialProgress = g_ships[0].legElapsedMinutes;
  check(partialProgress > 0 && g_campaign.coins == cashAtSail &&
        g_campaign.standbyAccountedRealSeconds == half, "partial voyage and watermark commit together");
#if SH_X4_STANDBY_DIAGNOSTICS
  const auto* deltaEvidence = diagnostic(X4_SD_DELTA, X4_SD_TIMER, 0);
  const auto* timerEvidence = diagnostic(X4_SD_ADVANCE_SAVE, X4_SD_TIMER, 0);
  check(deltaEvidence && (deltaEvidence->flags & X4_SD_TRUSTED) && deltaEvidence->a == half &&
        deltaEvidence->b == half && deltaEvidence->c == 0, "timer diagnostics expose trusted target and pending delta");
  check(timerEvidence && timerEvidence->session == 1234 && timerEvidence->a == half &&
        timerEvidence->b == g_campaign.worldMinutes && timerEvidence->d == (uint32_t)cashAtSail,
        "timer save evidence matches the committed watermark, world time and money");
#endif
  check(save_read(), "coherent checkpoint reloads after reset");
  check(board_x4_test_standby_resume(epoch + (uint64_t)half * 1000000u, true, false) &&
        g_ships[0].legElapsedMinutes == partialProgress && g_campaign.coins == cashAtSail,
        "repeated timer wake does not advance twice");

  // A crash before an outer commit drops both simulated cash and watermark.
  // Sim's own arrival save is suspended, exactly as the board controller does.
  save_suspend(true);
  g_campaign.standbyAccountedRealSeconds = duration;
  sim_advance_minutes((duration - half) * SIM_WORLD_MINUTES_PER_REAL_SECOND);
  save_suspend(false);
  check(save_read() && g_campaign.coins == cashAtSail &&
        g_campaign.standbyAccountedRealSeconds == half,
        "interrupted outer commit reloads the whole pre-arrival checkpoint");
  check(board_x4_test_standby_resume(epoch + (uint64_t)duration * 1000000u, true, false),
        "reset retries the uncommitted elapsed interval");
  check(g_campaign.coins == cashAtSail + arrivalCash && g_campaign.deliveredUnits == 4 &&
        g_ships[0].booking.state == BOOKING_SETTLED,
        "retry banks one booked payment and four delivered units");
#if SH_X4_STANDBY_DIAGNOSTICS
  timerEvidence = diagnostic(X4_SD_ADVANCE_SAVE, X4_SD_TIMER, 0);
  check(timerEvidence && timerEvidence->a == duration && timerEvidence->c == 4 &&
        timerEvidence->d == (uint32_t)(cashAtSail + arrivalCash),
        "silent timer arrival evidence precedes any interactive wake");
#endif
  int32_t arrivedCoins = g_campaign.coins;
  uint32_t arrivedWorld = g_campaign.worldMinutes;
  check(save_read(), "settled checkpoint reloads");
  board_x4_test_standby_resume(epoch + (uint64_t)duration * 1000000u, true, true);
#if SH_X4_STANDBY_DIAGNOSTICS
  check(diagnostic(X4_SD_ADVANCE_SAVE, X4_SD_TIMER, 0) &&
        !diagnostic(X4_SD_ADVANCE_SAVE, X4_SD_INTERACTIVE, 0),
        "later interactive wake cannot relabel an existing timer payment as manual catch-up");
#endif
  board_x4_test_standby_resume(epoch + 86400000000ull, true, true);
  check(g_campaign.coins == arrivedCoins && g_campaign.deliveredUnits == 4 &&
        g_campaign.worldMinutes == arrivedWorld && g_ships[0].state != SHIP_SAILING,
        "arrival waits without repeat payout, idle fees, or automatic redispatch");

  if (!begin()) return false;
  uint32_t beforeWorld = g_campaign.worldMinutes;
  check(!board_x4_test_standby_resume(epoch + 120000000u, false, true) &&
        g_campaign.coins == cashAtSail && g_campaign.worldMinutes == beforeWorld &&
        g_campaign.standbySessionId == 0,
        "power loss/brownout resumes saved company without invented absence time");
  if (!begin()) return false;
  g_campaign.paused = 1; save_write();
  beforeWorld = g_campaign.worldMinutes;
  check(board_x4_test_standby_resume(epoch + 600000000u, true, true) &&
        g_campaign.coins == cashAtSail && g_campaign.worldMinutes == beforeWorld,
        "paused company cannot gain standby travel");

  if (!begin()) return false;
  g_ships[0].booking = {};
  g_ships[0].state = SHIP_AWAITING_DECISION;
  g_incident = Incident{1, 0, INCIDENT_RESCUE, RISK_EVEN};
  save_write(); beforeWorld = g_campaign.worldMinutes;
  check(board_x4_test_standby_resume(epoch + 600000000u, true, true) &&
        g_ships[0].state == SHIP_AWAITING_DECISION && g_incident.active &&
        g_campaign.worldMinutes == beforeWorld && g_campaign.coins == cashAtSail,
        "legacy decision remains unresolved and earns no invented payment");

  if (!begin()) return false;
  const char* directory = getenv("SH_FS_DIR");
  if (!directory) return check(false, "test requires isolated SH_FS_DIR");
  char blockedPath[1024];
  snprintf(blockedPath, sizeof(blockedPath), "%s/sh_company.tmp", directory);
  check(::mkdir(blockedPath, 0755) == 0, "inject a failed snapshot write");
  check(!board_x4_test_standby_resume(epoch + (uint64_t)duration * 1000000u, true, false) &&
        g_campaign.coins == cashAtSail && g_campaign.deliveredUnits == 0 &&
        g_ships[0].state == SHIP_SAILING,
        "persistent save failure reloads prior progress instead of exposing uncommitted payment");
#if SH_X4_STANDBY_DIAGNOSTICS
  check(diagnostic(X4_SD_ADVANCE_SAVE, X4_SD_TIMER, -1) &&
        !diagnostic(X4_SD_ADVANCE_SAVE, X4_SD_TIMER, 0),
        "failed physical-snapshot writes never appear as successful timer commits");
#endif
  check(::rmdir(blockedPath) == 0, "remove snapshot write failure");
  check(save_read(), "last coherent snapshot survives failed writes");
  check(!board_x4_test_standby_resume(epoch + (uint64_t)duration * 1000000u, true, true) &&
        g_campaign.coins == cashAtSail && g_campaign.standbySessionId == 0,
        "aborted standby clock cannot replay after normal play resumes");

  // A voluntary POWER action must not discard unsaved gameplay on a healthy
  // battery. At the depleted threshold, keeping the rail up is not safe.
  int32_t unsavedCoins = ++g_campaign.coins;
  check(::mkdir(blockedPath, 0755) == 0, "inject voluntary shutdown save failure");
  board_x4_shutdown();
  check(!board_x4_asleep() && g_campaign.coins == unsavedCoins,
        "voluntary shutdown refuses failed commit and preserves progress in RAM");
  check(::rmdir(blockedPath) == 0 && save_write(), "progress saves after storage recovers");
  const char* previousBattery = getenv("SH_BATTERY");
  std::string batterySetting = previousBattery ? previousBattery : "";
  const char* previousUsb = getenv("SH_USB");
  std::string usbSetting = previousUsb ? previousUsb : "";
  bool hadBattery = previousBattery != nullptr, hadUsb = previousUsb != nullptr;
  setenv("SH_BATTERY", "2", 1); unsetenv("SH_USB");
  ++g_campaign.coins;
  check(::mkdir(blockedPath, 0755) == 0, "inject depleted shutdown save failure");
  board_x4_shutdown();
  check(board_x4_asleep(), "depleted battery shuts down after best-effort save");
  check(::rmdir(blockedPath) == 0, "remove depleted shutdown save failure");
  if (hadBattery) setenv("SH_BATTERY", batterySetting.c_str(), 1); else unsetenv("SH_BATTERY");
  if (hadUsb) setenv("SH_USB", usbSetting.c_str(), 1); else unsetenv("SH_USB");
  ::printf("standby integration: %s (%d assertions, real sim and atomic snapshots)\n",
           passed ? "PASS" : "FAIL", checks);
  return passed;
}
