// Minimal Arduino API mock for host-native unit tests.
//
// Provides just enough surface area for Heater.cpp, SimplePID.cpp, and the
// other peripheral classes in lib/GaggiMateController to compile and run
// natively. The mock is deliberately small — we only implement what the
// safety-test paths actually exercise.
//
// Design:
// - Pin state recorded in a global array; tests assert via getPinState().
// - millis()/micros() driven by a mock clock the test controls via
//   mock_arduino::setMillis(ms).
// - String is the minimal String class needed for ESP_LOG macros and a few
//   constructors in the production code.
//
// Used by [env:native_heater_safety] and any future native test envs that
// link against arduino-targeted code.

#pragma once
#ifndef NATIVE_MOCKS_ARDUINO_H
#define NATIVE_MOCKS_ARDUINO_H

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Production builds: ESP-IDF makes the ESP_LOG* macros visible globally via
// the Arduino-on-ESP-IDF header bundle. Mirror that here so client code (e.g.
// Heater.cpp) using ESP_LOGE/I/W without an explicit #include still compiles.
#include "esp_log.h"

// FreeRTOS types (TickType_t, TaskHandle_t) are also visible globally on
// arduino-esp32 via Arduino.h's transitive includes. FreeRTOS.h is the
// type-only piece — include it here, BEFORE we declare mock_arduino. The
// task.h include comes at the END of this file, after mock_arduino is
// declared (since the inline xTaskDelayUntil/vTaskDelay bodies reference
// mock_arduino::advanceMillis).
#include "freertos/FreeRTOS.h"

// ---------------------------------------------------------------------------
// Arduino constants
// ---------------------------------------------------------------------------

#define HIGH 0x1
#define LOW  0x0

#define OUTPUT          0x03
#define INPUT           0x01
#define INPUT_PULLUP    0x05
#define INPUT_PULLDOWN  0x09

#ifndef PROGMEM
#define PROGMEM
#endif

// Arduino-style math constants + helpers used by SimplePID and friends.
#ifndef PI
#define PI 3.1415926535897932384626433832795f
#endif
#ifndef HALF_PI
#define HALF_PI 1.5707963267948966192313216916398f
#endif
#ifndef TWO_PI
#define TWO_PI 6.283185307179586476925286766559f
#endif

template <typename T> static inline T constrain(T x, T low, T high) {
    return (x < low) ? low : ((x > high) ? high : x);
}
template <typename T> static inline T arduino_min(T a, T b) { return (a < b) ? a : b; }
template <typename T> static inline T arduino_max(T a, T b) { return (a > b) ? a : b; }
// `min`/`max` are macros in Arduino.h on real hardware; collisions with
// std::min/max are common pain. Avoid the macros here; tests use std::min/max.

// ---------------------------------------------------------------------------
// Mock state — accessed by tests via mock_arduino::*
// ---------------------------------------------------------------------------

namespace mock_arduino {
// One slot per GPIO pin. ESP32-S3 has up to 49 GPIOs; 64 gives padding for
// boards that map peripherals beyond the SoC's native GPIO range.
inline constexpr std::size_t MAX_PINS = 64;

// Pin output state recorded by digitalWrite. -1 = never written.
inline std::array<int, MAX_PINS> pinState{};

// Pin mode (OUTPUT, INPUT, etc.) recorded by pinMode.
inline std::array<int, MAX_PINS> pinMode_{};

// Mock clock — tests advance this; millis()/micros() read from it.
inline uint64_t mockMillis = 0;
inline uint64_t mockMicros = 0;

inline void reset() {
    pinState.fill(-1);
    pinMode_.fill(-1);
    mockMillis = 0;
    mockMicros = 0;
}

inline int getPinState(uint8_t pin) {
    return pin < MAX_PINS ? pinState[pin] : -1;
}

inline int getPinMode(uint8_t pin) {
    return pin < MAX_PINS ? pinMode_[pin] : -1;
}

inline void setMillis(uint64_t ms) {
    mockMillis = ms;
    mockMicros = ms * 1000;
}

inline void advanceMillis(uint64_t ms) {
    mockMillis += ms;
    mockMicros += ms * 1000;
}
} // namespace mock_arduino

// ---------------------------------------------------------------------------
// Arduino API surface
// ---------------------------------------------------------------------------

inline void pinMode(uint8_t pin, uint8_t mode) {
    if (pin < mock_arduino::MAX_PINS) mock_arduino::pinMode_[pin] = mode;
}

inline void digitalWrite(uint8_t pin, uint8_t val) {
    if (pin < mock_arduino::MAX_PINS) mock_arduino::pinState[pin] = val;
}

inline int digitalRead(uint8_t pin) {
    if (pin < mock_arduino::MAX_PINS && mock_arduino::pinState[pin] >= 0) {
        return mock_arduino::pinState[pin];
    }
    return 0;
}

inline unsigned long millis() { return static_cast<unsigned long>(mock_arduino::mockMillis); }
inline unsigned long micros() { return static_cast<unsigned long>(mock_arduino::mockMicros); }

inline void delay(unsigned long ms) { mock_arduino::advanceMillis(ms); }
inline void delayMicroseconds(unsigned int us) { mock_arduino::mockMicros += us; }

// PROGMEM string macro used by some code. Real ESP32 stores in flash; we
// just return the pointer.
#ifndef F
#define F(s) (s)
#endif

inline long random(long howbig) {
    return std::rand() % (howbig > 0 ? howbig : 1);
}

inline void randomSeed(unsigned long seed) { std::srand(static_cast<unsigned int>(seed)); }

// ---------------------------------------------------------------------------
// Minimal String — many gaggimate code paths construct String values. Just
// enough surface for compilation; tests don't validate String semantics.
// ---------------------------------------------------------------------------

class String : public std::string {
  public:
    String() = default;
    String(const char *s) : std::string(s ? s : "") {}
    String(const std::string &s) : std::string(s) {}
    String(int v) : std::string(std::to_string(v)) {}
    String(unsigned int v) : std::string(std::to_string(v)) {}
    String(long v) : std::string(std::to_string(v)) {}
    String(unsigned long v) : std::string(std::to_string(v)) {}
    String(float v, int = 2) : std::string(std::to_string(v)) {}
    String(double v, int = 2) : std::string(std::to_string(v)) {}

    const char *c_str() const { return std::string::c_str(); }
    size_t length() const { return std::string::length(); }

    String operator+(const String &rhs) const {
        return String(static_cast<const std::string &>(*this) + static_cast<const std::string &>(rhs));
    }
    String operator+(const char *rhs) const {
        return String(static_cast<const std::string &>(*this) + rhs);
    }
    String &operator+=(const String &rhs) {
        std::string::operator+=(rhs);
        return *this;
    }
    String &operator+=(const char *rhs) {
        std::string::operator+=(rhs);
        return *this;
    }

    bool operator==(const String &rhs) const {
        return static_cast<const std::string &>(*this) == static_cast<const std::string &>(rhs);
    }
    bool operator==(const char *rhs) const {
        return static_cast<const std::string &>(*this) == rhs;
    }
};

// ESP32 efuse MAC — return a fixed dummy value
inline uint64_t ESP_getEfuseMac() { return 0xDEADBEEFCAFE; }

// Minimal ESP class used by Arduino code (.getEfuseMac() etc.)
class _ESP {
  public:
    uint64_t getEfuseMac() { return ESP_getEfuseMac(); }
};
inline _ESP ESP;

inline uint32_t esp_random() { return static_cast<uint32_t>(std::rand()); }

// task.h reference inline functions that touch mock_arduino — must come
// AFTER the namespace declaration above.
#include "freertos/task.h"

#endif // NATIVE_MOCKS_ARDUINO_H
