#pragma once

#include <stdint.h>

namespace config {
// Empirical UD-CO2S self-heating offset; set to 0 to disable compensation.
constexpr float temperatureOffset = 4.5f;
constexpr int alarmThreshold = 1000;
constexpr int alarmClearThreshold = 900;
constexpr uint32_t alarmIntervalMs = 10000;
constexpr uint32_t staleAfterMs = 10000;
constexpr uint32_t startRetryMs = 5000;
constexpr uint16_t toneFrequency = 2000;
constexpr uint32_t toneDurationMs = 200;
constexpr uint8_t volume = 80;
static_assert(alarmClearThreshold < alarmThreshold, "Alarm needs hysteresis");
}
