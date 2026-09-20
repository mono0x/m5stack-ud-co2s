#include <M5Unified.h>
#include <SPI.h>
#include <cdcacm.h>
#include <driver/gpio.h>

#include "config.h"
#include "measurement.h"

namespace {
class SensorInit : public CDCAsyncOper {
    uint8_t OnInit(ACM* device) override {
        LINE_CODING coding = {115200, 0, 0, 8};
        uint8_t status = device->SetLineCoding(&coding);
        if (!status) status = device->SetControlLineState(3);
        Serial.printf("CDC init: 0x%02x\n", status);
        return status;
    }
};

USB usb;
SensorInit sensorInit;
ACM sensor(&usb, &sensorInit);
MeasurementParser parser;
Measurement measurement;
Alarm co2Alarm;
M5Canvas canvas(&M5.Display);
bool hostReady = false;
bool connected = false;
bool hasMeasurement = false;
uint32_t lastMeasurement = 0;
uint32_t lastStart = 0;
bool displayedFresh = false;
uint32_t lastPoll = 0;
uint8_t lastReceiveError = 0;

void startMeasurement(uint32_t now) {
    uint8_t command[] = {'S', 'T', 'A', '\r', '\n'};
    const uint8_t status = sensor.SndData(sizeof(command), command);
    Serial.printf("STA: 0x%02x\n", status);
    lastStart = now;
}

uint32_t co2Color(int ppm) {
    // LED ranges from the UD-CO2S manual; bright tints for the black background.
    if (ppm <= 1000) return 0x92B6FF;
    if (ppm <= 1500) return 0x92FFAA;
    if (ppm <= 2500) return 0xFFFFAA;
    if (ppm <= 3500) return 0xFF92AA;
    return 0xDB92FF;
}

void draw(bool fresh) {
    if (canvas.getBuffer() == nullptr) return;
    auto& display = canvas;
    const Measurement ambient = compensateTemperature(measurement, config::temperatureOffset);
    const uint32_t color = co2Color(measurement.co2);
    display.fillScreen(TFT_BLACK);
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(12, 12);
    display.print("UD-CO2S");
    display.setCursor(12, 48);
    if (!hostReady) display.print("USB host init failed");
    else if (!connected) display.print("Connect UD-CO2S");
    else if (!fresh) display.print(hasMeasurement ? "Data timeout" : "Waiting for data");
    else display.print(co2Alarm.isActive() ? "CO2 HIGH" : "Monitoring");

    display.setTextColor(fresh ? color : TFT_DARKGREY, TFT_BLACK);
    display.setTextSize(5);
    display.setCursor(12, 88);
    if (fresh) display.printf("%d", measurement.co2);
    else display.print("----");
    display.setTextSize(2);
    display.setCursor(245, 110);
    display.print("ppm");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(12, 158);
    if (fresh) display.printf("%.1f C   %.1f %%", ambient.temperature, ambient.humidity);
    else display.print("--.- C   --.- %");
    display.setCursor(12, 205);
    display.printf("Alarm >= %d ppm", config::alarmThreshold);
    // GPIO 35 is LCD D/C during transfer and MAX3421E MISO during USB transfers.
    gpio_set_direction(GPIO_NUM_35, GPIO_MODE_OUTPUT);
    canvas.pushSprite(0, 0);
    M5.Display.waitDisplay();
    gpio_set_direction(GPIO_NUM_35, GPIO_MODE_INPUT);
    displayedFresh = fresh;
}
}

void setup() {
    // Arduino SPI.begin resets SPI2. Do this before M5GFX configures the LCD bus.
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);
    SPI.begin(36, 35, 37, 1);

    auto settings = M5.config();
    settings.internal_spk = true;
    M5.begin(settings);
    Serial.begin(115200);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(128);
    canvas.setColorDepth(8);
    if (canvas.createSprite(M5.Display.width(), M5.Display.height()) == nullptr) {
        Serial.println("Display buffer allocation failed");
        gpio_set_direction(GPIO_NUM_35, GPIO_MODE_OUTPUT);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.println("Display buffer allocation failed");
        M5.Display.waitDisplay();
    }
    Serial.printf("Display: board=%d, size=%dx%d\n", static_cast<int>(M5.Display.getBoard()),
                  M5.Display.width(), M5.Display.height());
    M5.Speaker.setVolume(config::volume);
    M5.Power.setExtOutput(true);

    gpio_set_direction(GPIO_NUM_35, GPIO_MODE_INPUT);
    hostReady = usb.Init() == 0;
    Serial.printf("USB host: %s\n", hostReady ? "ready" : "failed");
    draw(false);
}

void loop() {
    const Measurement previousMeasurement = measurement;
    const bool previouslyConnected = connected;
    const bool previouslyActive = co2Alarm.isActive();
    M5.update();
    if (hostReady) usb.Task();
    uint32_t now = millis();
    const bool ready = hostReady && sensor.isReady();
    if (ready != connected) {
        connected = ready;
        hasMeasurement = false;
        parser.reset();
        lastReceiveError = 0;
        Serial.println(connected ? "Sensor connected" : "Sensor disconnected");
        if (connected) startMeasurement(now);
    }

    if (connected && uint32_t(now - lastPoll) >= 10) {
        lastPoll = now;
        uint8_t bytes[64];
        uint16_t count = sizeof(bytes);
        const uint8_t status = sensor.RcvData(&count, bytes);
        if (!status) {
            lastReceiveError = 0;
            for (uint16_t i = 0; i < count; ++i) {
                if (parser.feed(static_cast<char>(bytes[i]), measurement)) {
                    hasMeasurement = true;
                    lastMeasurement = millis();
                    Serial.printf("CO2=%d,HUM=%.1f,TMP=%.1f\n", measurement.co2,
                                  measurement.humidity, measurement.temperature);
                    const Measurement ambient = compensateTemperature(measurement, config::temperatureOffset);
                    Serial.printf("Estimated: HUM=%.1f,TMP=%.1f (offset=%.1f C)\n",
                                  ambient.humidity, ambient.temperature, config::temperatureOffset);
                }
            }
        } else if (status != hrNAK) {
            parser.reset();
            if (status != lastReceiveError) Serial.printf("USB receive: 0x%02x\n", status);
            lastReceiveError = status;
        }
    }

    now = millis();
    const bool fresh = connected && hasMeasurement &&
                       uint32_t(now - lastMeasurement) < config::staleAfterMs;
    if (connected && !fresh && uint32_t(now - lastStart) >= config::startRetryMs) {
        parser.reset();
        startMeasurement(now);
    }
    if (co2Alarm.update(measurement.co2, fresh, now, config::alarmThreshold,
                     config::alarmClearThreshold, config::alarmIntervalMs)) {
        M5.Speaker.tone(config::toneFrequency, config::toneDurationMs);
    }
    if (!co2Alarm.isActive()) M5.Speaker.stop();
    const bool measurementChanged = fresh &&
        (measurement.co2 != previousMeasurement.co2 ||
         measurement.humidity != previousMeasurement.humidity ||
         measurement.temperature != previousMeasurement.temperature);
    if (measurementChanged || connected != previouslyConnected ||
        fresh != displayedFresh || co2Alarm.isActive() != previouslyActive) {
        draw(fresh);
    }
    delay(1);
}
