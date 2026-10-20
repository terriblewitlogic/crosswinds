// Host stand-in for the small Arduino API surface used by Crosswinds. The desktop
// build puts this directory first on its include path; device builds never see
// this file. The SDL host uses wall-clock millis and writes Serial to stdout.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <algorithm>   // Arduino's min()/max() are macros; the game calls plain
                       // min(...)/max(...), so pull the std names into scope
#include <chrono>
#include <thread>

#ifndef PROGMEM
#define PROGMEM
#endif

using std::max;
using std::min;

inline uint32_t millis() {
  static const auto t0 = std::chrono::steady_clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
}

inline void delay(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ESP32-Arduino compatibility for libraries that probe the PSRAM allocator.
inline void* ps_malloc(size_t n) { return malloc(n); }

struct HostSerial {
  void begin(unsigned long) {}
  void setTxTimeoutMs(unsigned) {}
  template <class... A> void printf(const char* f, A... a) { ::printf(f, a...); }
  void println(const char* s = "") { ::printf("%s\n", s); }
  void print(const char* s) { ::printf("%s", s); }
};
static HostSerial Serial;

struct HostESP { unsigned getFreeHeap() { return 0; } };
static HostESP ESP;
