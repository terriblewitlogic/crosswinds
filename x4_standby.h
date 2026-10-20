// Retained-clock standby arithmetic. No wall-clock synchronization or platform
// calls live here; the board validates reset provenance before asking to resume.
#pragma once
#include <stdint.h>

#ifndef SH_X4_VOYAGE_STANDBY
#define SH_X4_VOYAGE_STANDBY 0
#endif

// RTC-only observability for a physical standby test. No shipping allocation,
// serial output, save changes or extra wakeups when this remains disabled.
#ifndef SH_X4_STANDBY_DIAGNOSTICS
#define SH_X4_STANDBY_DIAGNOSTICS 0
#endif

struct X4StandbyRecord {
  uint32_t magic;
  uint32_t session;
  uint32_t seed;
  uint32_t version;
  uint64_t startedUs;
  uint32_t checksum;
};

static constexpr uint32_t X4_STANDBY_MAGIC = 0x58445342u;
static constexpr uint32_t X4_STANDBY_VERSION = 1;
static constexpr uint32_t X4_STANDBY_MAX_SECONDS = 7u * 24u * 60u * 60u;
static constexpr uint32_t X4_STANDBY_MAX_WAKE_SECONDS = 15u * 60u;

inline uint32_t x4_standby_checksum(const X4StandbyRecord& record) {
  uint32_t h = 2166136261u;
  const uint32_t values[] = {record.magic, record.session, record.seed, record.version,
                            (uint32_t)record.startedUs, (uint32_t)(record.startedUs >> 32)};
  for (uint32_t value : values)
    for (uint8_t i = 0; i < 4; ++i) { h ^= (uint8_t)(value >> (i * 8)); h *= 16777619u; }
  return h;
}

inline X4StandbyRecord x4_standby_record(uint32_t session, uint32_t seed, uint64_t nowUs) {
  X4StandbyRecord record{X4_STANDBY_MAGIC, session, seed, X4_STANDBY_VERSION, nowUs, 0};
  record.checksum = x4_standby_checksum(record);
  return record;
}

inline bool x4_standby_valid(const X4StandbyRecord& record) {
  return record.magic == X4_STANDBY_MAGIC && record.version == X4_STANDBY_VERSION &&
         record.session != 0 && record.startedUs != 0 &&
         record.checksum == x4_standby_checksum(record);
}

struct X4StandbyDelta {
  bool trusted;
  uint32_t targetSeconds;
  uint32_t pendingSeconds;
};

inline X4StandbyDelta x4_standby_delta(const X4StandbyRecord& record, bool retainedClock,
                                      uint32_t savedSession, uint32_t savedSeed,
                                      uint32_t accountedSeconds, uint64_t nowUs) {
  if (!retainedClock || !x4_standby_valid(record) || savedSession != record.session ||
      savedSeed != record.seed || nowUs < record.startedUs) return {false, 0, 0};
  uint64_t seconds = (nowUs - record.startedUs) / 1000000u;
  if (seconds > X4_STANDBY_MAX_SECONDS || accountedSeconds > seconds) return {false, 0, 0};
  uint32_t target = (uint32_t)seconds;
  return {true, target, target - accountedSeconds};
}

inline uint32_t x4_standby_wake_seconds(uint32_t remainingWorldMinutes,
                                       uint32_t worldMinutesPerRealSecond) {
  if (!remainingWorldMinutes || !worldMinutesPerRealSecond) return 1;
  uint32_t seconds = remainingWorldMinutes / worldMinutesPerRealSecond +
                     (remainingWorldMinutes % worldMinutesPerRealSecond != 0);
  return seconds < X4_STANDBY_MAX_WAKE_SECONDS ? seconds : X4_STANDBY_MAX_WAKE_SECONDS;
}

enum X4StandbyDiagnosticPhase : uint32_t {
  X4_SD_BOOT = 1, X4_SD_LOAD, X4_SD_BEGIN_SAVE, X4_SD_DELTA,
  X4_SD_ADVANCE_SAVE, X4_SD_CLOSE_SAVE, X4_SD_ABORT_SAVE,
  X4_SD_SLEEP_SETUP, X4_SD_SLEEP_REJECT, X4_SD_SHUTDOWN_SAVE
};
enum X4StandbyDiagnosticWake : uint32_t {
  X4_SD_AWAKE, X4_SD_TIMER, X4_SD_INTERACTIVE, X4_SD_RESET
};
enum X4StandbyDiagnosticFlags : uint32_t {
  X4_SD_TRUSTED = 1, X4_SD_TIMER_ARMED = 2, X4_SD_GPIO_ARMED = 4,
  X4_SD_LATCH_HIGH = 8, X4_SD_SD_HIGH = 16, X4_SD_LATCH_HELD = 32,
  X4_SD_SD_HELD = 64, X4_SD_USB_PRESENT = 128
};
struct X4StandbyDiagnosticEvent {
  uint32_t sequence, session, phase, wake, flags;
  int32_t status;
  uint32_t a, b, c, d;
};
static constexpr uint32_t X4_STANDBY_DIAGNOSTIC_MAGIC = 0x58444431u;
static constexpr uint32_t X4_STANDBY_DIAGNOSTIC_CAPACITY = 12;
struct X4StandbyDiagnostics {
  uint32_t magic, nextSequence, head, count, dropped, checksum;
  X4StandbyDiagnosticEvent events[X4_STANDBY_DIAGNOSTIC_CAPACITY];
};
static_assert(sizeof(X4StandbyDiagnostics) <= 512, "Keep retained diagnostics bounded");

inline uint32_t x4_standby_diagnostic_checksum(const X4StandbyDiagnostics& record) {
  uint32_t hash = 2166136261u;
  auto mix = [&](uint32_t value) {
    for (uint8_t i = 0; i < 4; ++i) { hash ^= (uint8_t)(value >> (i * 8)); hash *= 16777619u; }
  };
  mix(record.magic); mix(record.nextSequence); mix(record.head); mix(record.count); mix(record.dropped);
  for (const auto& event : record.events) {
    mix(event.sequence); mix(event.session); mix(event.phase); mix(event.wake); mix(event.flags);
    mix((uint32_t)event.status); mix(event.a); mix(event.b); mix(event.c); mix(event.d);
  }
  return hash;
}

inline bool x4_standby_diagnostics_valid(const X4StandbyDiagnostics& record) {
  return record.magic == X4_STANDBY_DIAGNOSTIC_MAGIC && record.nextSequence != 0 &&
    record.head < X4_STANDBY_DIAGNOSTIC_CAPACITY && record.count <= X4_STANDBY_DIAGNOSTIC_CAPACITY &&
    record.checksum == x4_standby_diagnostic_checksum(record);
}

inline void x4_standby_diagnostics_reset(X4StandbyDiagnostics& record) {
  record = {};
  record.magic = X4_STANDBY_DIAGNOSTIC_MAGIC;
  record.nextSequence = 1;
  record.checksum = x4_standby_diagnostic_checksum(record);
}

inline void x4_standby_diagnostic_append(X4StandbyDiagnostics& record, X4StandbyDiagnosticEvent event) {
  if (!x4_standby_diagnostics_valid(record)) x4_standby_diagnostics_reset(record);
  // The checksum is installed last. A reset during this update invalidates
  // evidence rather than fabricating a successful flash commit.
  event.sequence = record.nextSequence++;
  if (!record.nextSequence) record.nextSequence = 1;
  record.events[record.head] = event;
  record.head = (record.head + 1) % X4_STANDBY_DIAGNOSTIC_CAPACITY;
  if (record.count < X4_STANDBY_DIAGNOSTIC_CAPACITY) ++record.count;
  else if (record.dropped != UINT32_MAX) ++record.dropped;
  record.checksum = x4_standby_diagnostic_checksum(record);
}

inline const X4StandbyDiagnosticEvent* x4_standby_diagnostic_at(const X4StandbyDiagnostics& record,
                                                              uint32_t index) {
  if (!x4_standby_diagnostics_valid(record) || index >= record.count) return nullptr;
  return &record.events[(record.head + X4_STANDBY_DIAGNOSTIC_CAPACITY - record.count + index) %
                       X4_STANDBY_DIAGNOSTIC_CAPACITY];
}
