// Minimal mock for robtillaart/MAX31855 library.
//
// Heater.h transitively includes Max31855Thermocouple.h which includes
// <MAX31855.h>. We don't instantiate Max31855Thermocouple in tests (we
// substitute TestSensor implementing the TemperatureSensor interface), so
// this mock only needs to satisfy the parse — an empty class declaration
// is enough to compile Max31855Thermocouple.h's `MAX31855 *max31855` field.

#pragma once
#ifndef NATIVE_MOCKS_MAX31855_H
#define NATIVE_MOCKS_MAX31855_H

#include <cstdint>

class MAX31855 {
  public:
    MAX31855() = default;
    explicit MAX31855(int /*select_pin*/) {}
    MAX31855(int /*sck_pin*/, int /*miso_pin*/, int /*cs_pin*/) {}

    void begin() {}
    int read() { return 0; }
    double getTemperature() { return 0.0; }
    double getInternal() { return 0.0; }
    uint32_t getRawData() { return 0; }
    bool getOpen() { return false; }
    bool getShortToGnd() { return false; }
    bool getShortToVcc() { return false; }
};

#endif // NATIVE_MOCKS_MAX31855_H
