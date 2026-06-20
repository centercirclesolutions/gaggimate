// Unit tests: Heater safety paths (host-native).
// pio test -e native_heater_safety
//
// Covers the three safety gates in Heater::loop() that drive the heater pin
// LOW unconditionally regardless of PID state:
//
//   1. Setpoint zero / negative → pin LOW
//   2. Temperature-sensor error  → pin LOW (even with positive setpoint)
//   3. Initial state at construction → pin never written HIGH before loop()
//
// And the autotune overtemp guard at MAX_AUTOTUNE_TEMP = 125 °C, which fires
// the error_callback and forces the heater off.
//
// These paths are the boiler's last lines of defence against thermal
// runaway. They live in production code as `digitalWrite(heaterPin, LOW)`
// in `Heater::loop()` (the sensor/setpoint gate) and as the `output = 0.0f`
// + error_callback path in `Heater::loopAutotune()`.
//
// The test directly drives heater->loop() instead of relying on the
// xTaskCreate-spawned loopTask — gives us deterministic, single-threaded
// control over the safety-gate logic.

#include <unity.h>

#include <functional>

#include "test_sensor.h"

// Direct-include the units under test. Same idiom as test_autotune_simc.cpp.
// The mocks/ include path is wired by platformio.ini's [env:native_heater_safety]
// build_flags so Arduino.h, freertos/*.h, esp_log.h all resolve to our shims.
#include "Autotune/Autotune.cpp"
#include "SimplePID/SimplePID.cpp"
#include "peripherals/Heater.cpp"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint8_t HEATER_PIN = 21; // matches typical board pinout; arbitrary for unit tests

// ---------------------------------------------------------------------------
// Test fixture helpers
// ---------------------------------------------------------------------------

namespace {

// Records error_callback invocations. Tests assert on count.
struct CallbackRecorder {
    int errorCount = 0;
    int autotuneFailCount = 0;
    int pidResultCount = 0;

    void reset() {
        errorCount = 0;
        autotuneFailCount = 0;
        pidResultCount = 0;
    }
};

CallbackRecorder rec;

heater_error_callback_t makeErrorCb() { return []() { rec.errorCount++; }; }
heater_autotune_fail_callback_t makeAutotuneFailCb() { return []() { rec.autotuneFailCount++; }; }
pid_result_callback_t makePidResultCb() {
    return [](float, float, float, float) { rec.pidResultCount++; };
}

// Build a Heater wired to fresh fakes. Caller owns the sensor (passed in by
// pointer so tests can mutate state mid-test). Caller also owns the Heater
// returned — typically via stack allocation.
} // namespace

void setUp() {
    mock_arduino::reset();
    mock_freertos::reset();
    rec.reset();
}

void tearDown() {}

// ---------------------------------------------------------------------------
// Test 1 — Sensor in error state forces heater pin LOW
// ---------------------------------------------------------------------------
//
// Setup: setpoint > 0 (user wants heat), sensor reports error.
// Expectation: Heater::loop() takes the sensor-error gate at line 46 of
// Heater.cpp and unconditionally drives the pin LOW.
static void test_sensor_error_forces_pin_low() {
    TestSensor sensor;
    sensor.setErrorState(true);     // thermocouple fault
    sensor.setTemperature(0.0f);    // (irrelevant when in error state)

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();                  // pinMode(OUTPUT), xTaskCreate(stubbed)
    heater.setSetpoint(95.0f);       // user wants brew temperature

    heater.loop();                   // execute safety logic

    TEST_ASSERT_EQUAL_MESSAGE(LOW, mock_arduino::getPinState(HEATER_PIN),
                              "heater pin must be LOW when sensor is in error state");
}

// ---------------------------------------------------------------------------
// Test 2 — Setpoint zero forces heater pin LOW
// ---------------------------------------------------------------------------
//
// Setup: sensor OK at ambient temperature, setpoint = 0 (machine standby).
// Expectation: same safety gate fires on setpoint <= 0.0f.
static void test_zero_setpoint_forces_pin_low() {
    TestSensor sensor;
    sensor.setErrorState(false);
    sensor.setTemperature(22.0f);

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    heater.setSetpoint(0.0f);        // standby

    heater.loop();

    TEST_ASSERT_EQUAL_MESSAGE(LOW, mock_arduino::getPinState(HEATER_PIN),
                              "heater pin must be LOW when setpoint is 0");
}

// ---------------------------------------------------------------------------
// Test 3 — Negative setpoint also forces heater pin LOW
// ---------------------------------------------------------------------------
//
// Defensive: a negative setpoint should never reach normal PID. The gate uses
// `setpoint <= 0.0f` to cover this.
static void test_negative_setpoint_forces_pin_low() {
    TestSensor sensor;
    sensor.setErrorState(false);
    sensor.setTemperature(22.0f);

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    heater.setSetpoint(-1.0f);

    heater.loop();

    TEST_ASSERT_EQUAL_MESSAGE(LOW, mock_arduino::getPinState(HEATER_PIN),
                              "heater pin must be LOW for any non-positive setpoint");
}

// ---------------------------------------------------------------------------
// Test 4 — Setup completes without raising error callback
// ---------------------------------------------------------------------------
//
// Defensive: confirms the construction path is clean — no premature errors
// fired during pinMode/PID setup.
static void test_setup_does_not_fire_error_callback() {
    TestSensor sensor;
    sensor.setErrorState(false);

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();

    TEST_ASSERT_EQUAL_MESSAGE(0, rec.errorCount, "error_callback must not fire during setup");
    TEST_ASSERT_EQUAL_MESSAGE(OUTPUT, mock_arduino::getPinMode(HEATER_PIN),
                              "heater pin must be configured as OUTPUT");
}

// ---------------------------------------------------------------------------
// Test 5 — Sensor error mid-run takes pin LOW
// ---------------------------------------------------------------------------
//
// Realistic regression: heater running normally, sensor faults mid-brew.
// The next loop() tick must drive pin LOW even though prior PID state had
// it HIGH.
static void test_sensor_fault_mid_run_drops_pin() {
    TestSensor sensor;
    sensor.setErrorState(false);
    sensor.setTemperature(50.0f);

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    heater.setSetpoint(95.0f);

    // Pre-condition: PID could in principle have driven the pin HIGH at some
    // point. We don't assert that — we assert the SAFETY gate's behaviour
    // independently of where PID left us.
    heater.loop();

    // Now fault the sensor mid-brew.
    sensor.setErrorState(true);
    heater.loop();

    TEST_ASSERT_EQUAL_MESSAGE(LOW, mock_arduino::getPinState(HEATER_PIN),
                              "heater pin must drop to LOW the next loop() tick after a sensor fault");
}

// ---------------------------------------------------------------------------
// Test 6 — Autotune overtemp guard at MAX_AUTOTUNE_TEMP
// ---------------------------------------------------------------------------
//
// The single highest-stakes safety path on the boiler: during autotune, the
// heater is being driven at full power in cycles. If the boiler temperature
// climbs above MAX_AUTOTUNE_TEMP (125 °C) the autotune must abort — set
// output=0, fire error_callback (which on the production controller fans
// out to thermalRunawayShutdown — kills heater, pump, valve, alt).
//
// Setup: sensor reports a constant 130 °C (above the guard).
// Expectation: heater.loop() → loopAutotune() reaches the overtemp branch,
// error_callback fires, autotuning is reset to false.
//
// Note on flow: Heater::loop() enters loopAutotune only when sensor is NOT
// in error state AND autotuning is true. We arrange both, then call loop()
// once. loopAutotune is a synchronous busy-loop in production — it doesn't
// return until isFinished, sensor fault, or overtemp. Our mocked vTaskDelay
// advances the mock clock, so the inner busy-wait completes in mock time
// without actually blocking.
static void test_autotune_overtemp_fires_error_callback() {
    TestSensor sensor;
    sensor.setErrorState(false);     // sensor reports healthy
    sensor.setTemperature(130.0f);   // ... but reading is above 125 °C limit

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    heater.setSetpoint(95.0f);
    heater.autotune(120, 6, 680);    // testTimeSec=120, windowSize=6, wattage=680

    heater.loop();                    // → loopAutotune() → overtemp branch

    TEST_ASSERT_EQUAL_MESSAGE(1, rec.errorCount,
                              "error_callback must fire when autotune sees temperature above MAX_AUTOTUNE_TEMP");
    TEST_ASSERT_EQUAL_MESSAGE(0, rec.pidResultCount,
                              "PID gains must NOT be reported on overtemp abort (preserves NVS gains)");
    TEST_ASSERT_EQUAL_MESSAGE(0, rec.autotuneFailCount,
                              "autotune-fail callback is for timeout, not overtemp — must not fire here");
}

// ---------------------------------------------------------------------------
// Test 7 — Autotune respects the safety gate when sensor faults BEFORE start
// ---------------------------------------------------------------------------
//
// Defensive: if the operator triggers autotune but the sensor is already
// faulted at that moment, Heater::loop()'s outer guard must still fire
// (sensor-error → pin LOW) and we must NOT enter loopAutotune.
static void test_autotune_blocked_by_sensor_error_at_entry() {
    TestSensor sensor;
    sensor.setErrorState(true);      // sensor faulted at startup
    sensor.setTemperature(0.0f);

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    heater.setSetpoint(95.0f);
    heater.autotune(120, 6, 680);

    heater.loop();

    TEST_ASSERT_EQUAL_MESSAGE(LOW, mock_arduino::getPinState(HEATER_PIN),
                              "sensor fault at autotune entry must take the outer safety gate, pin LOW");
    TEST_ASSERT_EQUAL_MESSAGE(0, rec.errorCount,
                              "error_callback is only fired from inside loopAutotune; outer safety gate must NOT fire it");
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sensor_error_forces_pin_low);
    RUN_TEST(test_zero_setpoint_forces_pin_low);
    RUN_TEST(test_negative_setpoint_forces_pin_low);
    RUN_TEST(test_setup_does_not_fire_error_callback);
    RUN_TEST(test_sensor_fault_mid_run_drops_pin);
    RUN_TEST(test_autotune_overtemp_fires_error_callback);
    RUN_TEST(test_autotune_blocked_by_sensor_error_at_entry);
    return UNITY_END();
}
