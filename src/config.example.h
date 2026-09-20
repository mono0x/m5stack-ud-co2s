#pragma once

#include <stdint.h>

namespace config {
// 1: original landscape, 3: inverted landscape, 0/2: portrait.
constexpr uint8_t displayRotation = 1;
static_assert(displayRotation <= 3, "Display rotation must be between 0 and 3");
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
