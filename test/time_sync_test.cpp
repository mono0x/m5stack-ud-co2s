#include "../src/time_sync.h"

#include <cassert>

int main() {
    for (int hour = 0; hour < 24; ++hour) {
        assert(isNightHour(hour, 22, 7) == (hour >= 22 || hour < 7));
        assert(isNightHour(hour, 9, 17) == (hour >= 9 && hour < 17));
        assert(!isNightHour(hour, 7, 7));
    }
    TimeSyncSchedule schedule;
    assert(schedule.isDue(0));
    schedule.finish(100, true);
    assert(!schedule.isDue(100 + config::timeSyncIntervalMs - 1));
    assert(schedule.isDue(100 + config::timeSyncIntervalMs));
    schedule.finish(200, false);
    assert(!schedule.isDue(200 + config::timeSyncRetryMs - 1));
    assert(schedule.isDue(200 + config::timeSyncRetryMs));
    const uint32_t beforeWrap = UINT32_MAX - 1000;
    schedule.finish(beforeWrap, true);
    assert(!schedule.isDue(beforeWrap + config::timeSyncIntervalMs - 1));
    assert(schedule.isDue(beforeWrap + config::timeSyncIntervalMs));
}
