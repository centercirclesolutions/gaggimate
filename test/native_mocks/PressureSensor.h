// Mock PressureSensor — same public API as the production class plus a
// setRawPressure() injector for tests. Lives in test/native_mocks/ and
// wins over the real lib/GaggiMateController/src/peripherals/PressureSensor.h
// via -I include priority in [env:native_pump_safety].
//
// DimmedPump consumes this through a PressureSensor* pointer and only
// calls getRawPressure(). The mock returns whatever the test injected.

// Match the production header guard so we ARE the PressureSensor.h that
// downstream code sees — both files have the same guard symbol, the first
// one parsed wins, and the -I path priority ensures it's ours.
#pragma once
#ifndef PRESSURESENSOR_H
#define PRESSURESENSOR_H

#include <functional>
#include <Arduino.h>

constexpr int PRESSURE_READ_INTERVAL_MS = 30;
constexpr float ADC_STEP = 6.144f / 32767.0f;

using pressure_callback_t = std::function<void(float)>;

class PressureSensor {
  public:
    PressureSensor() = default;
    PressureSensor(uint8_t /*sda*/, uint8_t /*scl*/, const pressure_callback_t & /*cb*/,
                   float /*pressure_scale*/ = 16.0f, float /*voltage_floor*/ = 0.5f,
                   float /*voltage_ceil*/ = 4.5f) {}

    void setup() {}
    void loop() {}
    float getPressure() const { return _pressure; }
    float getRawPressure() const { return _raw_pressure; }
    void setScale(float scale) { _pressure_scale = scale; }

    // Test-only injectors
    void setRawPressure(float p) { _raw_pressure = p; }
    void setFilteredPressure(float p) { _pressure = p; }

  private:
    float _pressure = 0.0f;
    float _raw_pressure = 0.0f;
    float _pressure_scale = 16.0f;
};

#endif // PRESSURESENSOR_H
