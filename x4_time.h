#pragma once
#include "game.h"
#include <stdio.h>

// Prospective wall-clock duration at the selected speed, excluding pauses and
// time spent waiting for orders. Division first keeps UINT32_MAX safe.
static inline uint32_t x4_real_seconds(uint32_t worldMinutes, uint8_t speed) {
  const uint32_t rate = SIM_WORLD_MINUTES_PER_REAL_SECOND * (speed ? speed : 1u);
  return worldMinutes / rate + (worldMinutes % rate != 0);
}

static inline void x4_format_duration(char* out, size_t cap, uint32_t seconds,
                                      bool compact = false) {
  if (!out || !cap) return;
  if (seconds < 60u) {
    snprintf(out, cap, "%lus", (unsigned long)seconds);
  } else if (compact) {
    const uint32_t minutes = seconds / 60u + (seconds % 60u != 0);
    snprintf(out, cap, "%lum", (unsigned long)minutes);
  } else {
    snprintf(out, cap, "%lum %02lus", (unsigned long)(seconds / 60u),
             (unsigned long)(seconds % 60u));
  }
}

static inline void x4_format_time_delta(char* out, size_t cap, int32_t worldDelta,
                                        uint8_t speed) {
  if (!out || !cap) return;
  // Unsigned subtraction also handles INT32_MIN without signed overflow.
  const uint32_t magnitude = worldDelta < 0 ? 0u - (uint32_t)worldDelta :
                                             (uint32_t)worldDelta;
  char duration[24];
  x4_format_duration(duration, sizeof(duration), x4_real_seconds(magnitude, speed));
  snprintf(out, cap, "%s%s", worldDelta > 0 ? "+" : worldDelta < 0 ? "-" : "", duration);
}
