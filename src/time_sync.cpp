#include "time_sync.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <atomic>
#include <time.h>

namespace {
enum class SyncState { Idle, Connecting, Synchronizing };
SyncState state = SyncState::Idle;
TimeSyncSchedule schedule;
std::atomic<bool> synchronized{false};
bool hasTime = false;
uint32_t started = 0;
uint32_t lastBrightnessCheck = 0;
uint8_t brightness = config::displayBrightness;

void finish(uint32_t now, bool success) {
    esp_sntp_stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    state = SyncState::Idle;
    schedule.finish(now, success);
    if (success) {
        hasTime = true;
        const time_t current = time(nullptr);
        struct tm local;
        localtime_r(&current, &local);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
        Serial.printf("Time synchronized: %s; Wi-Fi off\n", timestamp);
    } else {
        Serial.println("Time sync timed out; Wi-Fi off, retry scheduled");
    }
}
}

void beginTimeSync() {
    setenv("TZ", config::timeZone, 1);
    tzset();
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    // The SNTP callback runs on the network task, not the Arduino loop task.
    sntp_set_time_sync_notification_cb([](struct timeval*) { synchronized.store(true); });
    if (config::wifiSsid[0] == '\0') {
        Serial.println("Time sync disabled: configure Wi-Fi in config.h");
    }
    updateTimeSync(millis());
}

void updateTimeSync(uint32_t now) {
    if (config::wifiSsid[0] == '\0') return;
    if (state == SyncState::Idle && schedule.isDue(now)) {
        synchronized.store(false);
        started = now;
        state = SyncState::Connecting;
        WiFi.mode(WIFI_STA);
        WiFi.begin(config::wifiSsid, config::wifiPassword);
        Serial.println("Time sync: connecting Wi-Fi");
    }
    if (state == SyncState::Connecting && WiFi.status() == WL_CONNECTED) {
        configTzTime(config::timeZone, config::ntpServer);
        state = SyncState::Synchronizing;
        Serial.println("Time sync: waiting for NTP");
    }
    if (state != SyncState::Idle) {
        if (synchronized.load()) finish(now, true);
        else if (uint32_t(now - started) >= config::timeSyncTimeoutMs) finish(now, false);
    }
    if (hasTime && uint32_t(now - lastBrightnessCheck) >= 1000) {
        lastBrightnessCheck = now;
        const time_t current = time(nullptr);
        struct tm local;
        localtime_r(&current, &local);
        const uint8_t target = isNightHour(local.tm_hour, config::nightStartHour,
                                          config::nightEndHour)
                                   ? config::nightBrightness : config::displayBrightness;
        if (target != brightness) {
            M5.Display.setBrightness(target);
            brightness = target;
            Serial.printf("Display brightness: %u\n", brightness);
        }
    }
}
