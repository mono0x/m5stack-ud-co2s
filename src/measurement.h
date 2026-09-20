#pragma once

#include <cmath>
#include <cstdio>
#include <stddef.h>
#include <stdint.h>

struct Measurement {
    int co2 = 0;
    float humidity = 0;
    float temperature = 0;
};

// Apply to validated raw samples, never to an already compensated measurement.
inline Measurement compensateTemperature(const Measurement& raw, float offset) {
    Measurement result = raw;
    result.temperature = raw.temperature - offset;
    // Preserve water vapor partial pressure using the Tetens saturation formula.
    const float exponent = 7.5f * raw.temperature / (raw.temperature + 237.3f) -
                           7.5f * result.temperature / (result.temperature + 237.3f);
    result.humidity = raw.humidity * std::pow(10.0f, exponent);
    result.humidity = std::fmin(100.0f, std::fmax(0.0f, result.humidity));
    return result;
}

class MeasurementParser {
public:
    bool feed(char byte, Measurement& result) {
        if (byte == '\n') {
            buffer[length] = '\0';
            bool valid = !discarding && parse(result);
            reset();
            return valid;
        }
        if (discarding) return false;
        if (byte == '\0' || length == sizeof(buffer) - 1) {
            discarding = true;
            return false;
        }
        buffer[length++] = byte;
        return false;
    }

    void reset() {
        length = 0;
        discarding = false;
    }

private:
    bool parse(Measurement& result) {
        Measurement value;
        int end = 0;
        // Bound the integer conversion; accept only a complete measurement line.
        if (std::sscanf(buffer, "CO2=%5d,HUM=%f,TMP=%f%n", &value.co2,
                        &value.humidity, &value.temperature, &end) != 3) return false;
        if (buffer[end] == '\r') ++end;
        if (buffer[end] != '\0' || value.co2 < 400 || value.co2 > 10000 ||
            !std::isfinite(value.humidity) || !std::isfinite(value.temperature) ||
            value.humidity < 0 || value.humidity > 100 ||
            value.temperature < -40 || value.temperature > 70) return false;
        result = value;
        return true;
    }

    char buffer[96] = {};
    size_t length = 0;
    bool discarding = false;
};
