// Unit tests: DimmedPump safety paths (host-native).
// pio test -e native_pump_safety
//
// DimmedPump is the second peripheral with safety implications after the
// heater. The pump drives the boiler at up to MAX_PRESSURE (15 bar);
// runaway overpressure can split bowls, blow out the steam path, or
// rupture safety valves. The safety contract:
//
//   1. setPower(0) → PSM commanded to 0%   (the canonical "OFF" path)
//   2. setPower(s)  → clamped to [0, 100]   (defensive against bad input)
//   3. setPower(0) → _ctrlPressure forced to 0 (pressure target gated)
//   4. setPower(s) → _currentFlow zeroed when power is zero
//   5. setPower(s) → mode flips to POWER (overrides PRESSURE/FLOW modes)
//
// The production DimmedPump.cpp implementation is ~90 lines; we direct-
// include it like the Heater pattern. PSM gets a recording mock; the
// PressureSensor is replaced with our test-injectable variant via the
// native_mocks include path. PressureController is included as-is — it's
// pure data math (millis() only used in a commented-out log).

#include <unity.h>

#include <cstdint>

#include "PSM.h"             // mock recorder
#include "PressureSensor.h"  // test-injectable mock

// Direct-include production code under test.
#include "PressureController/PressureController.cpp"
#include "SimpleKalmanFilter/SimpleKalmanFilter.cpp"
#include "peripherals/DimmedPump.cpp"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint8_t SSR_PIN = 22;
static constexpr uint8_t SENSE_PIN = 23;

void setUp() {
    mock_arduino::reset();
    mock_freertos::reset();
    mock_psm::reset();
}
void tearDown() {}

// ---------------------------------------------------------------------------
// Test 1 — setPower(0) commands PSM to 0% (the canonical OFF path)
// ---------------------------------------------------------------------------
//
// The simplest, most-frequent safety call. Brew end → setPower(0) → pump
// must stop NOW. Direct verification that the PSM driver received the
// zero command.
static void test_set_power_zero_stops_psm() {
    PressureSensor sensor;
    DimmedPump pump(SSR_PIN, SENSE_PIN, &sensor);

    pump.setPower(0.0f);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_psm::lastSetValue,
                              "PSM must be commanded to 0% when setPower(0) is called");
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(1, mock_psm::setCallCount,
                                         "PSM.set must be called at least once");
}

// ---------------------------------------------------------------------------
// Test 2 — setPower(>100) clamps to 100
// ---------------------------------------------------------------------------
//
// Defensive against bad input. Even if a caller passes 1000 or a NaN-
// adjacent value, the PSM should never see anything above 100%.
static void test_set_power_above_max_clamps_to_100() {
    PressureSensor sensor;
    DimmedPump pump(SSR_PIN, SENSE_PIN, &sensor);

    pump.setPower(150.0f);

    TEST_ASSERT_EQUAL_MESSAGE(100, mock_psm::lastSetValue,
                              "PSM commanded value must be clamped to 100");
}

// ---------------------------------------------------------------------------
// Test 3 — setPower(negative) clamps to 0
// ---------------------------------------------------------------------------
static void test_set_power_negative_clamps_to_zero() {
    PressureSensor sensor;
    DimmedPump pump(SSR_PIN, SENSE_PIN, &sensor);

    pump.setPower(-25.0f);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_psm::lastSetValue,
                              "PSM commanded value must be clamped to 0 on negative input");
}

// ---------------------------------------------------------------------------
// Test 4 — setPower(s) passes through the integer value for valid s
// ---------------------------------------------------------------------------
//
// 50% should yield PSM.set(50). The implementation casts to int.
static void test_set_power_valid_passes_through() {
    PressureSensor sensor;
    DimmedPump pump(SSR_PIN, SENSE_PIN, &sensor);

    pump.setPower(50.0f);

    TEST_ASSERT_EQUAL_MESSAGE(50, mock_psm::lastSetValue,
                              "PSM commanded value must equal the int-cast of input");

    pump.setPower(0.5f);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_psm::lastSetValue,
                              "PSM commanded value must be 0 for sub-1% input (int cast)");
}

// ---------------------------------------------------------------------------
// Test 5 — setPower(0) zeroes _ctrlPressure (pressure target gated)
// ---------------------------------------------------------------------------
//
// Internal safety: when power is forced to 0, the pressure-control target
// is also reset. Prevents stale targets from re-engaging the pump on the
// next mode change. Observable via getPressureTarget().
static void test_set_power_zero_clears_pressure_target() {
    PressureSensor sensor;
    DimmedPump pump(SSR_PIN, SENSE_PIN, &sensor);

    pump.setPower(60.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(20.0f, pump.getPressureTarget(),
                                    "positive power should set pressure-target ceiling to 20 bar");

    pump.setPower(0.0f);
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, pump.getPressureTarget(),
                                    "setPower(0) must reset pressure-target to 0 bar");
}

// ---------------------------------------------------------------------------
// Test 6 — setPower(0) overrides pressure-target mode
// ---------------------------------------------------------------------------
//
// Realistic scenario: a brew sets up pressure target, then operator
// presses STOP. setPower(0) MUST take precedence over any prior
// setPressureTarget call.
static void test_set_power_zero_overrides_pressure_mode() {
    PressureSensor sensor;
    DimmedPump pump(SSR_PIN, SENSE_PIN, &sensor);

    pump.setPressureTarget(9.0f, 2.0f);    // pressure mode, target 9 bar
    pump.setPower(0.0f);                    // STOP

    // Mode should now be POWER (not PRESSURE), pressure target cleared,
    // PSM commanded to 0.
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_psm::lastSetValue,
                              "STOP after pressure-target must drive PSM to 0");
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, pump.getPressureTarget(),
                                    "STOP must clear the pressure target even if pressure mode was active");
}

// ---------------------------------------------------------------------------
// Test 7 — setPower(0) zeroes the flow estimate
// ---------------------------------------------------------------------------
//
// Internal state hygiene: when power goes to 0, the current flow
// estimate (used for thermal feedforward) must also drop to 0.
// Prevents stale flow from continuing to drive the heater FF gain.
static void test_set_power_zero_zeroes_flow_estimate() {
    PressureSensor sensor;
    DimmedPump pump(SSR_PIN, SENSE_PIN, &sensor);

    pump.setPower(50.0f);                   // primes some state
    pump.setPower(0.0f);

    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, pump.getPumpFlow(),
                                    "pump flow estimate must be 0 after setPower(0)");
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_set_power_zero_stops_psm);
    RUN_TEST(test_set_power_above_max_clamps_to_100);
    RUN_TEST(test_set_power_negative_clamps_to_zero);
    RUN_TEST(test_set_power_valid_passes_through);
    RUN_TEST(test_set_power_zero_clears_pressure_target);
    RUN_TEST(test_set_power_zero_overrides_pressure_mode);
    RUN_TEST(test_set_power_zero_zeroes_flow_estimate);
    return UNITY_END();
}
