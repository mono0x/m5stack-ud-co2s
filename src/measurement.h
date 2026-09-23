#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <stddef.h>
#include <stdint.h>

struct Measurement {
    int co2 = 0;
    float humidity = 0;
    float temperature = 0;
};

class MeasurementSampler {
public:
    bool update(const Measurement& incoming, uint32_t now, bool stateChanged) {
        if (available && !stateChanged && uint32_t(now - lastUpdate) < 60000) return false;
        value = incoming;
        lastUpdate = now;
        available = true;
        return true;
    }

    // Recovery accepts the next valid sample immediately.
    void invalidate() { available = false; }
    bool hasValue() const { return available; }
    const Measurement& current() const { return value; }

private:
    Measurement value;
    uint32_t lastUpdate = 0;
    bool available = false;
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

enum class ParseResult { Incomplete, Valid, OutOfRange, Invalid };
enum class ReceptionStatus { Waiting, Valid, OutOfRange, Invalid, Timeout };

class MeasurementReception {
public:
    void update(ParseResult result, uint32_t now) {
        if (result == ParseResult::Incomplete) return;
        lastReceived = now;
        state = result == ParseResult::Valid ? ReceptionStatus::Valid :
                result == ParseResult::OutOfRange ? ReceptionStatus::OutOfRange :
                ReceptionStatus::Invalid;
    }

    ReceptionStatus status(uint32_t now, uint32_t timeout) const {
        if (state == ReceptionStatus::Waiting) return state;
        return uint32_t(now - lastReceived) >= timeout ? ReceptionStatus::Timeout : state;
    }

private:
    uint32_t lastReceived = 0;
    ReceptionStatus state = ReceptionStatus::Waiting;
};

class MeasurementParser {
public:
    ParseResult feed(char byte, Measurement& result) {
        if (byte == '\n') {
            buffer[length] = '\0';
            const ParseResult status = discarding ? ParseResult::Invalid : parse(result);
            reset();
            return status;
        }
        if (discarding) return ParseResult::Incomplete;
        if (byte == '\0' || length == sizeof(buffer) - 1) {
            discarding = true;
            return ParseResult::Incomplete;
        }
        buffer[length++] = byte;
        return ParseResult::Incomplete;
    }

    void reset() {
        length = 0;
        discarding = false;
    }

private:
    ParseResult parse(Measurement& result) {
        Measurement value;
        char concentration[96];
        int end = 0;
        if (std::sscanf(buffer, "CO2=%95[+0123456789-],HUM=%f,TMP=%f%n", concentration,
                        &value.humidity, &value.temperature, &end) != 3) return ParseResult::Invalid;
        if (buffer[end] == '\r') ++end;
        if (buffer[end] != '\0' || !std::isfinite(value.humidity) ||
            !std::isfinite(value.temperature)) return ParseResult::Invalid;
        char* numberEnd;
        errno = 0;
        const long co2 = std::strtol(concentration, &numberEnd, 10);
        if (numberEnd == concentration || *numberEnd != '\0') return ParseResult::Invalid;
        if (errno == ERANGE || co2 < 400 || co2 > 10000 ||
            value.humidity < 0 || value.humidity > 100 ||
            value.temperature < -40 || value.temperature > 70) return ParseResult::OutOfRange;
        value.co2 = static_cast<int>(co2);
        result = value;
        return ParseResult::Valid;
    }

    char buffer[96] = {};
    size_t length = 0;
    bool discarding = false;
};
