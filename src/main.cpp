#include <M5Unified.h>
#include <SPI.h>
#include <cdcacm.h>
#include <driver/gpio.h>

#include "config.h"
#include "alarm.h"
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
MeasurementSampler sampler;
const Measurement& measurement = sampler.current();
Alarm co2Alarm;
M5Canvas canvas(&M5.Display);
bool hostReady = false;
bool connected = false;
MeasurementReception reception;
uint32_t lastStart = 0;
bool displayedFresh = false;
ReceptionStatus displayedStatus = ReceptionStatus::Waiting;
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
    const Co2Level level = co2Level(ppm);
    if (level == Co2Level::Blue) return 0x92B6FF;
    if (level == Co2Level::Green) return 0x92FFAA;
    if (level == Co2Level::Yellow) return 0xFFFFAA;
    if (level == Co2Level::Red) return 0xFF92AA;
    return 0xDB92FF;
}

void draw(bool fresh, ReceptionStatus receptionStatus) {
    if (canvas.getBuffer() == nullptr) return;
    auto& display = canvas;
    const Measurement ambient = compensateTemperature(measurement, config::temperatureOffset);
    const uint32_t color = co2Color(measurement.co2);
    const int width = display.width();
    const int height = display.height();
    const int co2Top = (height - 200) / 2;
    display.fillScreen(TFT_BLACK);
    display.setTextWrap(false);
    if (!fresh) {
        const char* status;
        if (!hostReady) status = "USB host init failed";
        else if (!connected) status = "Connect UD-CO2S";
        else if (receptionStatus == ReceptionStatus::Waiting) status = "Waiting for data";
        else if (receptionStatus == ReceptionStatus::OutOfRange) status = "Out of range";
        else if (receptionStatus == ReceptionStatus::Invalid) status = "Invalid data";
        else if (receptionStatus == ReceptionStatus::Timeout) status = "Data timeout";
        else status = "Waiting for update";
        display.setTextSize(2);
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        if (display.textWidth(status) > width - 24) display.setTextSize(1);
        display.setCursor((width - display.textWidth(status)) / 2, 4);
        display.print(status);
    }

    display.setTextColor(fresh ? color : TFT_DARKGREY, TFT_BLACK);
    char concentration[6] = "----";
    if (fresh) snprintf(concentration, sizeof(concentration), "%d", measurement.co2);
    int textSize = 10;
    display.setTextSize(textSize);
    while (display.textWidth(concentration) > width - 24 && textSize > 1) {
        display.setTextSize(--textSize);
    }
    display.setCursor((width - display.textWidth(concentration)) / 2, co2Top);
    display.print(concentration);
    display.setTextSize(2);
    display.setCursor((width - display.textWidth("ppm")) / 2, co2Top + 84);
    display.print("ppm");
    char temperature[16] = "--.- C";
    char humidity[16] = "--.- %";
    if (fresh) {
        snprintf(temperature, sizeof(temperature), "%.1f C", ambient.temperature);
        snprintf(humidity, sizeof(humidity), "%.1f %%", ambient.humidity);
    }
    display.setTextColor(fresh ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
    display.setTextSize(4);
    display.setCursor((width - display.textWidth(temperature)) / 2, height - 96);
    display.print(temperature);
    display.setCursor((width - display.textWidth(humidity)) / 2, height - 52);
    display.print(humidity);
    // GPIO 35 is LCD D/C during transfer and MAX3421E MISO during USB transfers.
    gpio_set_direction(GPIO_NUM_35, GPIO_MODE_OUTPUT);
    canvas.pushSprite(0, 0);
    M5.Display.waitDisplay();
    gpio_set_direction(GPIO_NUM_35, GPIO_MODE_INPUT);
    displayedFresh = fresh;
    displayedStatus = receptionStatus;
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
    M5.Display.setRotation(config::displayRotation);
    M5.Display.setBrightness(32);
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
    draw(false, ReceptionStatus::Waiting);
}

void loop() {
    const Measurement previousMeasurement = measurement;
    const bool previouslyConnected = connected;
    const bool previouslyActive = co2Alarm.isActive();
    bool stateChanged = false;
    M5.update();
    if (hostReady) usb.Task();
    uint32_t now = millis();
    const bool ready = hostReady && sensor.isReady();
    if (ready != connected) {
        stateChanged = true;
        connected = ready;
        reception = MeasurementReception();
        sampler.invalidate();
        parser.reset();
        lastReceiveError = 0;
        Serial.println(connected ? "Sensor connected" : "Sensor disconnected");
        if (connected) startMeasurement(now);
    }

    if (reception.status(now, config::staleAfterMs) == ReceptionStatus::Timeout) {
        sampler.invalidate();
    }
    if (connected && uint32_t(now - lastPoll) >= 10) {
        lastPoll = now;
        uint8_t bytes[64];
        uint16_t count = sizeof(bytes);
        const uint8_t status = sensor.RcvData(&count, bytes);
        if (!status) {
            lastReceiveError = 0;
            Measurement incoming;
            for (uint16_t i = 0; i < count; ++i) {
                const ParseResult result = parser.feed(static_cast<char>(bytes[i]), incoming);
                const uint32_t receivedAt = millis();
                const ReceptionStatus previousStatus = reception.status(receivedAt, config::staleAfterMs);
                reception.update(result, receivedAt);
                stateChanged |= reception.status(receivedAt, config::staleAfterMs) != previousStatus;
                if (result == ParseResult::OutOfRange || result == ParseResult::Invalid) {
                    sampler.invalidate();
                }
                if (result == ParseResult::Valid) {
                    const bool rearmed = co2Alarm.rearm(incoming.co2);
                    const bool colorChanged = co2Level(incoming.co2) != co2Level(measurement.co2);
                    stateChanged |= rearmed || colorChanged;
                    if (!sampler.update(incoming, receivedAt, stateChanged)) continue;
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
    const ReceptionStatus receptionStatus = reception.status(now, config::staleAfterMs);
    const bool fresh = connected && receptionStatus == ReceptionStatus::Valid && sampler.hasValue();
    const bool waiting = receptionStatus == ReceptionStatus::Waiting ||
                         receptionStatus == ReceptionStatus::Timeout;
    if (connected && waiting && uint32_t(now - lastStart) >= config::startRetryMs) {
        parser.reset();
        startMeasurement(now);
    }
    if (co2Alarm.update(measurement.co2, fresh, now)) {
        stateChanged = true;
        M5.Speaker.tone(config::toneFrequency, config::toneDurationMs);
    }
    if (!co2Alarm.isActive()) M5.Speaker.stop();
    const bool measurementChanged = fresh &&
        (measurement.co2 != previousMeasurement.co2 ||
         measurement.humidity != previousMeasurement.humidity ||
         measurement.temperature != previousMeasurement.temperature);
    if (stateChanged || measurementChanged || connected != previouslyConnected ||
        fresh != displayedFresh || receptionStatus != displayedStatus ||
        co2Alarm.isActive() != previouslyActive) {
        draw(fresh, receptionStatus);
    }
    delay(1);
}
