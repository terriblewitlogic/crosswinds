#include "x4_standby.h"
#include <cstdio>
#include <cstdlib>

static void check(bool result, const char* message) {
  if (result) return;
  std::fprintf(stderr, "x4 standby: FAIL: %s\n", message);
  std::exit(1);
}

int main() {
  constexpr uint64_t start = 23456789;
  X4StandbyRecord record = x4_standby_record(123, 42, start);
  check(x4_standby_valid(record), "retained record validates");
  auto delta = [&](uint32_t accounted, uint64_t elapsedUs) {
    return x4_standby_delta(record, true, 123, 42, accounted, start + elapsedUs);
  };
  auto first = delta(0, 120000000);
  check(first.trusted && first.pendingSeconds == 120, "timer wake accounts two real minutes");
  check(delta(120, 120999999).pendingSeconds == 0, "repeated wake cannot award committed time twice");
  check(delta(120, 180000000).pendingSeconds == 60, "successive wake advances only new time");

  // Simulated atomic snapshot: crash before commit leaves both clock and cash
  // old; crash after commit leaves both new. Repeating wake yields one payout.
  struct Snapshot { uint32_t accounted = 0, voyageSeconds = 0, cash = 500; bool arrived = false; };
  for (int interruptBeforeCommit = 0; interruptBeforeCommit < 2; ++interruptBeforeCommit) {
    Snapshot disk;
    auto reconcile = [&](bool commit) {
      Snapshot working = disk;
      auto pending = delta(working.accounted, 120000000);
      working.accounted = pending.targetSeconds;
      working.voyageSeconds += pending.pendingSeconds;
      if (!working.arrived && working.voyageSeconds >= 120) { working.cash += 100; working.arrived = true; }
      if (commit) disk = working;
    };
    reconcile(!interruptBeforeCommit);
    reconcile(true);
    reconcile(true);
    check(disk.accounted == 120 && disk.cash == 600 && disk.arrived,
          "whole-snapshot crash/replay settles one arrival exactly once");
  }

  auto rejected = [&](bool clock, uint32_t session, uint32_t seed, uint32_t accounted, uint64_t now) {
    return !x4_standby_delta(record, clock, session, seed, accounted, now).trusted;
  };
  check(rejected(false, 123, 42, 0, start + 120000000), "cold power/brownout cannot create elapsed time");
  check(rejected(true, 124, 42, 0, start + 120000000), "old RTC session cannot advance a new snapshot");
  check(rejected(true, 123, 43, 0, start + 120000000), "another company cannot inherit standby time");
  check(rejected(true, 123, 42, 121, start + 120000000), "clock rollback behind checkpoint is refused");
  check(rejected(true, 123, 42, 0, start - 1), "backward RTC is refused");
  check(rejected(true, 123, 42, 0, start + (uint64_t)(X4_STANDBY_MAX_SECONDS + 1) * 1000000),
        "unbounded clock jump is refused");
  record.startedUs ^= 1;
  check(!x4_standby_valid(record), "corrupted retained record cannot advance time");
  check(x4_standby_wake_seconds(1200, 10) == 120, "two-minute voyage wakes at arrival");
  check(x4_standby_wake_seconds(1201, 10) == 121, "wake duration rounds up");
  check(x4_standby_wake_seconds(1200, 160) == 8, "wake duration respects game speed");
  check(x4_standby_wake_seconds(UINT32_MAX, 10) == 900, "long voyage caps battery-check interval");

  X4StandbyDiagnostics evidence{};
  check(!x4_standby_diagnostics_valid(evidence), "uninitialized diagnostic RAM is not evidence");
  x4_standby_diagnostic_append(evidence, {0, 123, X4_SD_ADVANCE_SAVE, X4_SD_TIMER, 0, 0, 120, 1200, 4, 560});
  X4StandbyDiagnostics afterReset = evidence;
  x4_standby_diagnostic_append(afterReset, {0, 123, X4_SD_DELTA, X4_SD_INTERACTIVE, X4_SD_TRUSTED, 0, 130, 10, 120, 1200});
  const auto* committed = x4_standby_diagnostic_at(afterReset, 0);
  const auto* manual = x4_standby_diagnostic_at(afterReset, 1);
  check(committed && committed->wake == X4_SD_TIMER && committed->phase == X4_SD_ADVANCE_SAVE &&
        committed->status == 0 && committed->a == 120 && manual && manual->wake == X4_SD_INTERACTIVE &&
        manual->c == 120, "retained timer commit stays distinct from later manual catch-up");
  afterReset.events[afterReset.head].status = -1;  // interrupted append before header/checksum commit
  check(!x4_standby_diagnostics_valid(afterReset) && !x4_standby_diagnostic_at(afterReset, 0),
        "partially updated retained history cannot claim a successful commit");
  x4_standby_diagnostic_append(afterReset, {0, 124, X4_SD_BEGIN_SAVE, X4_SD_AWAKE, 0, -1, 0, 0, 0, 500});
  check(afterReset.count == 1 && x4_standby_diagnostic_at(afterReset, 0)->status == -1,
        "corrupt history resets and preserves an explicit failed-save result");
  x4_standby_diagnostics_reset(evidence);
  for (uint32_t i = 0; i < 20; ++i)
    x4_standby_diagnostic_append(evidence, {0, 123, X4_SD_ADVANCE_SAVE, X4_SD_TIMER, 0, 0, i, 0, 0, 0});
  check(evidence.count == 12 && evidence.dropped == 8 && x4_standby_diagnostic_at(evidence, 0)->a == 8 &&
        x4_standby_diagnostic_at(evidence, 11)->a == 19 && !x4_standby_diagnostic_at(evidence, 12),
        "bounded ring retains chronological recent history and reports overwritten evidence");
  std::puts("x4 standby: PASS (elapsed, replay, invalid clock/session, wake scheduling, retained diagnostic integrity/overflow)");
}
