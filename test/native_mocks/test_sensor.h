// Test double for TemperatureSensor — host-side fake the tests poke directly.
//
// The real TemperatureSensor base class declares two pure virtual methods
// (read, isErrorState). This fake lets tests command both values independently
// so we can simulate: sensor fault, sensor healthy at any temperature, etc.

#pragma once
#ifndef TEST_HEATER_SAFETY_TEST_SENSOR_H
#define TEST_HEATER_SAFETY_TEST_SENSOR_H

#include "peripherals/TemperatureSensor.h"

class TestSensor : public TemperatureSensor {
  public:
    float read() override { return _temperature; }
    bool isErrorState() override { return _errorState; }

    void setTemperature(float t) { _temperature = t; }
    void setErrorState(bool e) { _errorState = e; }

  private:
    float _temperature = 25.0f;
    bool _errorState = false;
};

#endif // TEST_HEATER_SAFETY_TEST_SENSOR_H
