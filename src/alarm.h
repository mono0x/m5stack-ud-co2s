#pragma once

#include "config.h"

enum class Co2Level { Blue, Green, Yellow, Red, Purple };

inline Co2Level co2Level(int ppm) {
    if (ppm <= config::greenThreshold) return Co2Level::Blue;
    if (ppm <= config::yellowThreshold) return Co2Level::Green;
    if (ppm <= config::redThreshold) return Co2Level::Yellow;
    if (ppm <= config::purpleThreshold) return Co2Level::Red;
    return Co2Level::Purple;
}

class Alarm {
public:
    // Called every loop; true requests one tone without blocking sensor polling.
    bool update(int ppm, bool fresh, uint32_t now) {
        const Co2Level next = fresh ? co2Level(ppm) : Co2Level::Blue;
        const bool worsened = next > level;
        if (next != level || !fresh) {
            remaining = 0;
        }
        level = next;
        if (!isActive()) return false;

        if (worsened) {
            remaining = level == Co2Level::Yellow ? 2 : 3;
            lastTone = now;
            --remaining;
            return true;
        }
        if (remaining && uint32_t(now - lastTone) >=
                         config::toneDurationMs + config::toneGapMs) {
            --remaining;
            lastTone = now;
            return true;
        }
        return false;
    }

    bool isActive() const { return level >= Co2Level::Yellow; }

private:
    Co2Level level = Co2Level::Blue;
    uint8_t remaining = 0;
    uint32_t lastTone = 0;
};
