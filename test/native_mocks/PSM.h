// Minimal mock for the PSM (Phase Shift Modulation) library used by
// DimmedPump for AC dimmer control of the pump SSR.
//
// Production PSM hooks attachInterrupt + a hardware timer to chop the AC
// waveform. For host-native tests we only need to record set() calls so
// we can verify "DimmedPump::setPower(0) → PSM commanded to 0%".

#pragma once
#ifndef NATIVE_MOCKS_PSM_H
#define NATIVE_MOCKS_PSM_H

#include "Arduino.h"

#ifndef RISING
#define RISING 1
#endif
#ifndef FALLING
#define FALLING 2
#endif

namespace mock_psm {
inline int lastSetValue = -1;     // -1 = never set
inline int setCallCount = 0;
inline void reset() {
    lastSetValue = -1;
    setCallCount = 0;
}
} // namespace mock_psm

class PSM {
  public:
    PSM(unsigned char sensePin, unsigned char controlPin, unsigned int range,
        int mode = RISING, unsigned char divider = 1, unsigned char interruptMinTimeDiff = 0) {
        (void)sensePin;
        (void)controlPin;
        (void)range;
        (void)mode;
        (void)divider;
        (void)interruptMinTimeDiff;
    }

    void set(unsigned int value) {
        mock_psm::lastSetValue = static_cast<int>(value);
        mock_psm::setCallCount++;
    }

    long getCounter() { return 0; }
    void resetCounter() {}
    void stopAfter(long /*counter*/) {}
    unsigned int cps() { return 50; } // simulate 50 Hz AC line freq
    unsigned long getLastMillis() { return 0; }
    unsigned char getDivider() { return 1; }
    void setDivider(unsigned char /*divider*/ = 1) {}
    void shiftDividerCounter(char /*value*/ = 1) {}
};

#endif // NATIVE_MOCKS_PSM_H
