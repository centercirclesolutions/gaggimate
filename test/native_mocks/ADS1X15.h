// Stub for robtillaart/ADS1X15 library — only needed to satisfy
// PressureSensor.h's #include. Tests don't instantiate ADS1115.

#pragma once
#ifndef NATIVE_MOCKS_ADS1X15_H
#define NATIVE_MOCKS_ADS1X15_H

#include <cstdint>

class ADS1115 {
  public:
    ADS1115() = default;
    explicit ADS1115(uint8_t /*address*/) {}
    void begin() {}
    bool isConnected() { return false; }
    int16_t readADC(uint8_t /*pin*/) { return 0; }
    int16_t readADC_Differential_0_1() { return 0; }
    void setGain(uint8_t /*gain*/) {}
    void setDataRate(uint8_t /*rate*/) {}
    void requestADC(uint8_t /*pin*/) {}
    bool isBusy() { return false; }
    int16_t getValue() { return 0; }
};

#endif // NATIVE_MOCKS_ADS1X15_H
