#pragma once
#include <Arduino.h>
// SDK diagnostics use the standalone USB serial stream.
#define LOG_INF(tag, fmt, ...) Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__)
