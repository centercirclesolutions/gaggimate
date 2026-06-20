// Test doubles for Pump, SimpleRelay, and Heater (just the surface
// handlePingTimeout interacts with). Each mock records the call count + last
// argument so the test can assert "setSetpoint(0) was called exactly once".

#pragma once
#ifndef TEST_PING_TIMEOUT_MOCK_PERIPHERALS_H
#define TEST_PING_TIMEOUT_MOCK_PERIPHERALS_H

#include "peripherals/Pump.h"

class MockPump : public Pump {
  public:
    void setup() override {}
    void loop() override {}
    void setPower(float setpoint) override {
        setPowerCount++;
        lastPower = setpoint;
    }

    int setPowerCount = 0;
    float lastPower = -1.0f; // sentinel — never set
};

// SimpleRelay isn't pure virtual; provide a stub that doesn't need the real
// Arduino-bound implementation (which writes to GPIO via digitalWrite).
class MockRelay {
  public:
    MockRelay() = default;

    void set(bool s) {
        setCount++;
        lastState = s;
    }
    bool getState() const { return lastState; }

    int setCount = 0;
    bool lastState = true; // sentinel: default to "on" so we can assert it changed
};

// Heater stub — only setSetpoint matters for the ping-timeout path. Reusing
// the real Heater here would drag in SimplePID/Autotune/SoftPWM; for a
// contract test we just need the call recorder.
class MockHeater {
  public:
    void setSetpoint(float s) {
        setSetpointCount++;
        lastSetpoint = s;
    }

    int setSetpointCount = 0;
    float lastSetpoint = -1.0f;
};

#endif // TEST_PING_TIMEOUT_MOCK_PERIPHERALS_H
