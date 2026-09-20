#include "../src/measurement.h"

#include <cassert>
#include <string>

bool feed(MeasurementParser& parser, const std::string& input, Measurement& result) {
    bool received = false;
    for (char byte : input) received = parser.feed(byte, result) || received;
    return received;
}

int main() {
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

    Alarm alarm;
    const auto update = [&alarm](int ppm, bool fresh, uint32_t now) {
        return alarm.update(ppm, fresh, now, 1000, 900, 10000);
    };
    assert(!update(999, true, 0));
    assert(update(1000, true, 1));
    assert(!update(950, true, 10000));
    assert(update(950, true, 10001));
    assert(!update(900, true, 10002) && !alarm.isActive());
    assert(!update(950, true, 10003));
    assert(update(1200, true, 10004));
    assert(!update(1200, false, 10005) && !alarm.isActive());
    assert(update(1200, true, UINT32_MAX - 5000));
    assert(!update(1200, true, 4998));
    assert(update(1200, true, 4999));
}
