#pragma once

#include <stdint.h>

namespace config {
// Empirical UD-CO2S self-heating offset; set to 0 to disable compensation.
constexpr float temperatureOffset = 4.5f;
constexpr int greenThreshold = 1000;
constexpr int yellowThreshold = 1500;
constexpr int redThreshold = 2500;
constexpr int purpleThreshold = 3500;
constexpr uint32_t staleAfterMs = 10000;
constexpr uint32_t startRetryMs = 5000;
constexpr uint16_t toneFrequency = 2000;
constexpr uint32_t toneDurationMs = 200;
constexpr uint32_t toneGapMs = 100;
constexpr uint8_t volume = 80;
static_assert(greenThreshold < yellowThreshold && yellowThreshold < redThreshold &&
              redThreshold < purpleThreshold, "CO2 levels must be ordered");
}
