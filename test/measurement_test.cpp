#include "../src/measurement.h"
#include "../src/alarm.h"

#include <cassert>
#include <string>

bool feed(MeasurementParser& parser, const std::string& input, Measurement& result) {
    bool received = false;
    for (char byte : input) received = (parser.feed(byte, result) == ParseResult::Valid) || received;
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
    assert(updateSample(60001));
    assert(sampler.hasValue());
    assert(!updateSample(120000));
    assert(updateSample(120001));
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

    const auto parseLine = [&](const std::string& line) {
        ParseResult result = ParseResult::Incomplete;
        for (char byte : line) result = parser.feed(byte, value);
        return result;
    };
    for (const auto* line : {"CO2=10001,HUM=50,TMP=20\n", "CO2=100000,HUM=50,TMP=20\n",
                             "CO2=999999999999999999999999999999,HUM=50,TMP=20\n",
                             "CO2=399,HUM=50,TMP=20\n", "CO2=-1,HUM=50,TMP=20\n",
                             "CO2=500,HUM=101,TMP=20\n", "CO2=500,HUM=-1,TMP=20\n",
                             "CO2=500,HUM=50,TMP=71\n", "CO2=500,HUM=50,TMP=-41\r\n"}) {
        assert(parseLine(line) == ParseResult::OutOfRange);
        assert(value.co2 == 500 && value.humidity == 50 && value.temperature == 20);
    }
    for (const auto* line : {"CO2=500,HUM=nan,TMP=20\n", "CO2=500,HUM=50,TMP=inf\n",
                             "CO2=500,HUM=50\n", "CO2=500,HUM=50,TMP=20junk\n",
                             "CO2=--1,HUM=50,TMP=20\n"}) {
        assert(parseLine(line) == ParseResult::Invalid);
    }
    assert(parseLine(std::string(100, 'x') + "\n") == ParseResult::Invalid);
    assert(parseLine(std::string("CO2=500\0,HUM=50,TMP=20\n", 23)) == ParseResult::Invalid);
    assert(parseLine("CO2=10000,HUM=100,TMP=70\r\n") == ParseResult::Valid);

    MeasurementReception reception;
    const uint32_t timeout = 10000;
    assert(reception.status(100000, timeout) == ReceptionStatus::Waiting);
    reception.update(ParseResult::Valid, 0);
    assert(reception.status(9999, timeout) == ReceptionStatus::Valid);
    assert(reception.status(10000, timeout) == ReceptionStatus::Timeout);
    for (uint32_t now = 10000; now <= 30000; now += 1000) {
        reception.update(parseLine("CO2=10001,HUM=50,TMP=20\n"), now);
        assert(reception.status(now + 999, timeout) == ReceptionStatus::OutOfRange);
    }
    reception.update(ParseResult::Incomplete, 39999);
    assert(reception.status(39999, timeout) == ReceptionStatus::OutOfRange);
    assert(reception.status(40000, timeout) == ReceptionStatus::Timeout);
    reception.update(ParseResult::Invalid, 40001);
    assert(reception.status(40001, timeout) == ReceptionStatus::Invalid);
    reception.update(ParseResult::Valid, 40002);
    assert(reception.status(40002, timeout) == ReceptionStatus::Valid);
    const uint32_t receptionStart = UINT32_MAX - 100;
    reception.update(ParseResult::OutOfRange, receptionStart);
    assert(reception.status(uint32_t(receptionStart + 9999), timeout) == ReceptionStatus::OutOfRange);
    assert(reception.status(uint32_t(receptionStart + 10000), timeout) == ReceptionStatus::Timeout);
    reception = MeasurementReception();
    assert(reception.status(0, timeout) == ReceptionStatus::Waiting);

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
    assert(!alarm.update(1501, true, 7 * interval + 2));
    assert(!alarm.update(1401, true, 7 * interval + 3));
    assert(!alarm.update(1501, true, 7 * interval + 4));
    assert(!alarm.update(1400, true, 7 * interval + 5));
    burst(1501, 7 * interval + 6, 2);

    // Missing data cancels the burst without rearming a notified level.
    assert(alarm.update(3501, true, 8 * interval));
    assert(!alarm.update(3501, false, 8 * interval + 1) && !alarm.isActive());
    assert(!alarm.update(3501, false, 8 * interval + spacing));
    assert(!alarm.update(3501, true, 9 * interval));
    assert(!alarm.update(3501, true, 9 * interval + spacing));
    assert(!alarm.update(1000, true, 10 * interval));
    assert(alarm.update(2501, true, 10 * interval + 1));
    assert(!alarm.update(1501, true, 10 * interval + 2));
    assert(!alarm.update(1501, true, 10 * interval + spacing + 1));

    // Each level rearms independently at exactly 100 ppm below its threshold.
    for (int threshold : {1500, 2500, 3500}) {
        alarm = Alarm();
        const int count = threshold == 1500 ? 2 : 3;
        burst(threshold + 1, 0, count);
        assert(!alarm.update(threshold, true, 2000));
        assert(!alarm.update(threshold + 1, true, 3000));
        assert(!alarm.update(threshold - 99, true, 4000));
        assert(!alarm.update(threshold + 1, true, 5000));
        assert(!alarm.update(threshold - 100, true, 6000));
        burst(threshold + 1, 7000, count);
    }

    alarm = Alarm();
    burst(1501, 0, 2);
    assert(!alarm.update(1500, true, 1000));
    assert(!alarm.update(1501, true, 2000));
    burst(2501, 3000, 3);
    assert(!alarm.update(2500, true, 4000));
    assert(!alarm.update(2501, true, 5000));
    burst(3501, 6000, 3);

    // Rearming forces an immediate sample update within the same color.
    alarm = Alarm();
    sampler = MeasurementSampler();
    burst(1501, 0, 2);
    assert(!alarm.update(1500, true, 1000));
    sample.co2 = 1500;
    assert(updateSample(1000));
    sample.co2 = 1401;
    assert(!alarm.rearm(sample.co2));
    assert(!updateSample(1100));
    sample.co2 = 1400;
    const bool rearmed = alarm.rearm(sample.co2);
    assert(rearmed);
    assert(sampler.update(sample, 1200, rearmed));
    assert(sampler.current().co2 == 1400);
    assert(!alarm.rearm(sample.co2));
    sample.co2 = 1399;
    assert(!updateSample(61199));
    assert(updateSample(61200));
    burst(1501, 2000, 2);

    alarm = Alarm();
    burst(1501, 0, 2);
    assert(!alarm.update(1000, false, 1000));
    assert(!alarm.update(1501, true, 2000));

    // Inter-tone spacing survives millis() rollover without repeating later.
    alarm = Alarm();
    burst(2501, UINT32_MAX - 100, 3);
    assert(!alarm.update(2501, true, interval - 102));
    assert(!alarm.update(2501, true, interval - 101));
}
