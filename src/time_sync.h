#pragma once

#include <stdint.h>

#include "config.h"

inline bool isNightHour(int hour, int start, int end) {
    return start < end ? hour >= start && hour < end :
           start > end && (hour >= start || hour < end);
}

class TimeSyncSchedule {
public:
    bool isDue(uint32_t now) const { return uint32_t(now - lastFinished) >= interval; }
    void finish(uint32_t now, bool success) {
        lastFinished = now;
        interval = success ? config::timeSyncIntervalMs : config::timeSyncRetryMs;
    }

private:
    uint32_t lastFinished = 0;
    uint32_t interval = 0;
};

void beginTimeSync();
void updateTimeSync(uint32_t now);
