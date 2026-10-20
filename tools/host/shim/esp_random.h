// Host shim for ESP32's esp_random.h; the sketch uses it to seed new
// campaigns from the hardware RNG; the host uses the C++ random device.
// SH_SEED pins the value, making a fresh campaign fully deterministic
// (screenshot-diff tests depend on this).
#pragma once
#include <cstdint>
#include <cstdlib>
#include <random>

inline uint32_t esp_random() {
  static const char* fixed = getenv("SH_SEED");
  if (fixed) return (uint32_t)strtoul(fixed, nullptr, 0);
  static std::mt19937 rng{std::random_device{}()};
  return (uint32_t)rng();
}
