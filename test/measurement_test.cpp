#include "../src/measurement.h"
#include "../src/alarm.h"

#include <cassert>
#include <string>

bool feed(MeasurementParser& parser, const std::string& input, Measurement& result) {
    bool received = false;
    for (char byte : input) received = parser.feed(byte, result) || received;
    return received;
}

int main() {
    MeasurementSampler sampler;
    Measurement sample;
    const auto updateSample = [&](uint32_t now) {
        return sampler.update(sample, now,
                              co2Level(sample.co2) != co2Level(sampler.current().co2));
    };
    sample.co2 = 1000;
    assert(!sampler.hasValue());
    assert(updateSample(0));
    assert(sampler.hasValue() && sampler.current().co2 == 1000);
    sample.co2 = 900;
    sample.humidity = 50;
    sample.temperature = 25;
    assert(!updateSample(59999));
    assert(sampler.current().co2 == 1000);
    assert(updateSample(60000));
    assert(sampler.current().co2 == 900 && sampler.current().humidity == 50 &&
           sampler.current().temperature == 25);
    sampler.invalidate();
    assert(!sampler.hasValue());
    assert(!updateSample(60001));
    assert(!sampler.hasValue());
    assert(updateSample(120000) && sampler.hasValue());
    sampler = MeasurementSampler();
    const uint32_t sampleStart = UINT32_MAX - 100;
    assert(updateSample(sampleStart));
    assert(!updateSample(uint32_t(sampleStart + 59999)));
    assert(updateSample(uint32_t(sampleStart + 60000)));

    // Crossing each color boundary updates immediately in both directions.
    for (int threshold : {config::greenThreshold, config::yellowThreshold,
                          config::redThreshold, config::purpleThreshold}) {
        sampler = MeasurementSampler();
        sample.co2 = threshold;
        assert(updateSample(0));
        sample.co2 = threshold + 1;
        assert(updateSample(1));
        assert(sampler.current().co2 == threshold + 1);
        sample.co2 = threshold + 2;
        assert(!updateSample(2));
        assert(!updateSample(60000));
        assert(updateSample(60001));
        sample.co2 = threshold;
        assert(updateSample(60002));
        sampler.invalidate();
        sample.co2 = threshold + 1;
        assert(updateSample(60003) && sampler.hasValue());
    }

    MeasurementParser parser;
    Measurement value;
    assert(!feed(parser, "STA\r\nOK\r\nCO2=123", value));
    assert(feed(parser, "4,HUM=45.6,TMP=-2.3\r\n", value));
    assert(value.co2 == 1234 && value.humidity == 45.6f && value.temperature == -2.3f);
    assert(feed(parser, "CO2=400,HUM=0,TMP=-40\nCO2=10000,HUM=100,TMP=70\n", value));
    assert(value.co2 == 10000);
    for (const auto* line : {"CO2=0,HUM=50,TMP=20\n", "CO2=999999999999,HUM=50,TMP=20\n",
                             "CO2=500,HUM=nan,TMP=20\n", "CO2=500,HUM=50,TMP=inf\n",
                             "CO2=500,HUM=101,TMP=20\n", "CO2=500,HUM=50,TMP=71\n",
                             "CO2=500,HUM=50,TMP=20junk\n", "CO2=500,HUM=50\n"}) {
        assert(!feed(parser, line, value));
        assert(value.co2 == 10000);
    }
    assert(!feed(parser, std::string(100, 'x') + "CO2=500,HUM=50,TMP=20\n", value));
    assert(!feed(parser, std::string("CO2=500\0,HUM=50,TMP=20\n", 23), value));
    assert(feed(parser, "CO2=500,HUM=50,TMP=20\n", value));
    feed(parser, "CO2=12", value);
    parser.reset();
    assert(!feed(parser, "34,HUM=50,TMP=20\n", value));

    Measurement raw;
    raw.co2 = 1234;
    raw.temperature = 30;
    raw.humidity = 50;
    const Measurement ambient = compensateTemperature(raw, 4.5f);
    assert(ambient.co2 == raw.co2 && ambient.temperature == 25.5f);
    assert(std::fabs(ambient.humidity - 65.01014f) < 0.001f);
    assert(raw.temperature == 30 && raw.humidity == 50);
    const Measurement disabled = compensateTemperature(raw, 0);
    assert(disabled.temperature == raw.temperature && disabled.humidity == raw.humidity);
    raw.temperature = 20;
    assert(std::fabs(compensateTemperature(raw, 4.5f).humidity - 66.38919f) < 0.001f);
    raw.temperature = -5;
    assert(std::fabs(compensateTemperature(raw, 4.5f).humidity - 70.84597f) < 0.001f);
    raw.temperature = 30;
    raw.humidity = 90;
    assert(compensateTemperature(raw, 4.5f).humidity == 100);
    raw.humidity = 0;
    assert(compensateTemperature(raw, 4.5f).humidity == 0);

    assert(co2Level(1000) == Co2Level::Blue);
    assert(co2Level(1001) == Co2Level::Green);
    assert(co2Level(1500) == Co2Level::Green);
    assert(co2Level(1501) == Co2Level::Yellow);
    assert(co2Level(2500) == Co2Level::Yellow);
    assert(co2Level(2501) == Co2Level::Red);
    assert(co2Level(3500) == Co2Level::Red);
    assert(co2Level(3501) == Co2Level::Purple);

    Alarm alarm;
    const uint32_t spacing = config::toneDurationMs + config::toneGapMs;
    const uint32_t interval = 5 * 60 * 1000;
    const auto burst = [&](int ppm, uint32_t now, int count) {
        for (int i = 0; i < count; ++i) {
            assert(alarm.update(ppm, true, now + i * spacing));
            assert(!alarm.update(ppm, true, now + (i + 1) * spacing - 1));
        }
        assert(!alarm.update(ppm, true, now + count * spacing));
    };
    assert(!alarm.update(1000, true, 0));
    assert(!alarm.update(1500, true, 1) && !alarm.isActive());
    burst(1501, 2, 2);
    assert(alarm.isActive());
    assert(!alarm.update(2500, true, interval + 2));
    burst(2501, interval + 3, 3);
    assert(!alarm.update(3500, true, 2 * interval + 2));
    assert(!alarm.update(3500, true, 2 * interval + 3));
    burst(3501, 2 * interval + 2000, 3);
    assert(!alarm.update(3501, true, 3 * interval + 2000));
    // Unchanged and improving levels stay silent, even after five minutes.
    assert(!alarm.update(2501, true, 4 * interval + 2000));
    assert(!alarm.update(2501, true, 5 * interval + 1999));
    assert(!alarm.update(2501, true, 5 * interval + 2000));
    assert(!alarm.update(1501, true, 6 * interval));
    assert(!alarm.update(1501, true, 7 * interval));
    assert(!alarm.update(1500, true, 7 * interval + 1) && !alarm.isActive());
    burst(1501, 7 * interval + 2, 2);

    // Missing data cancels the burst; fresh high readings start a new one.
    assert(alarm.update(3501, true, 8 * interval));
    assert(!alarm.update(3501, false, 8 * interval + 1) && !alarm.isActive());
    assert(!alarm.update(3501, false, 8 * interval + spacing));
    burst(3501, 9 * interval, 3);
    assert(!alarm.update(1000, true, 10 * interval));
    assert(alarm.update(2501, true, 10 * interval + 1));
    assert(!alarm.update(1501, true, 10 * interval + 2));
    assert(!alarm.update(1501, true, 10 * interval + spacing + 1));

    // Inter-tone spacing survives millis() rollover without repeating later.
    alarm = Alarm();
    burst(2501, UINT32_MAX - 100, 3);
    assert(!alarm.update(2501, true, interval - 102));
    assert(!alarm.update(2501, true, interval - 101));
}
