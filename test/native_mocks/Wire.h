// Minimal Wire (I2C) stub for host-native tests. PressureSensor.cpp
// includes Wire.h; we don't actually instantiate I2C in tests.

#pragma once
#ifndef NATIVE_MOCKS_WIRE_H
#define NATIVE_MOCKS_WIRE_H

class TwoWire {
  public:
    void begin() {}
    void begin(int /*sda*/, int /*scl*/) {}
};

extern TwoWire Wire;

#endif // NATIVE_MOCKS_WIRE_H
