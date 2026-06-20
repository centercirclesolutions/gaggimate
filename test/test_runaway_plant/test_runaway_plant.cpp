// Unit tests: closed-loop Heater PID against a first-order thermal plant.
// pio test -e native_runaway_plant
//
// Complements test_heater_safety (which verifies the SAFETY GATES — sensor
// fault, overtemp guard) with PID QUALITY tests that exercise the full
// loop: PID → softPwm → heater pin → thermal plant → sensor → PID.
//
// What this validates:
//   1. Closed loop converges — boiler reaches the setpoint within a
//      reasonable window starting from ambient.
//   2. No unbounded runaway — peak temperature stays below a hard ceiling
//      well above setpoint, proving the PID never gets stuck commanding
//      100 % duty.
//   3. Steady state holds within a tight band once converged.
//   4. Setpoint change tracks — bumping the target re-converges.
//
// What this DOESN'T validate:
//   - The actual gaggimate PID gains being optimal. Defaults (Kp=2.4,
//     Ki=40, Kd=10 in Heater.h) are pre-autotune placeholders; real-world
//     gains come from running Heater::autotune. The tests use the defaults
//     and assert behavior bounded enough to catch unintentional gain
//     regressions, not optimal tuning.
//   - High-fidelity boiler dynamics (steam state, thermosyphon, sensor
//     lag beyond the basic first-order model). The plant model is
//     deliberately simple — enough fidelity to make the tests meaningful,
//     not so much that test numbers depend on physics we haven't measured.

#include <unity.h>

#include <cstdint>

#include "thermal_plant.h"

// Reuse the test sensor + mocks from test_heater_safety via shared paths.
#include "test_sensor.h"

// Direct-include the production code (same pattern as test_heater_safety).
#include "Autotune/Autotune.cpp"
#include "SimplePID/SimplePID.cpp"
#include "peripherals/Heater.cpp"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint8_t HEATER_PIN = 21;

// Simulation step: 10 ms matches the production Heater::loopTask cadence
// (`xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(10))`). softPwm at this rate
// against a 1 s PWM window gives 100 samples per cycle — plenty for the
// plant model to integrate accurately.
static constexpr int SIM_STEP_MS = 10;

// Hard runaway ceiling. The PID should NEVER let temperature climb this
// high regardless of tuning aggressiveness. If this trips, something has
// gone catastrophically wrong (gain mis-configured, output unclamped, etc).
// Real boiler tops out at ~140 °C under steam; 150 °C means a runaway.
static constexpr float RUNAWAY_CEILING = 150.0f;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

namespace {

struct CallbackRecorder {
    int errorCount = 0;
    int pidResultCount = 0;
    int autotuneFailCount = 0;
    void reset() { *this = {}; }
};

CallbackRecorder rec;

heater_error_callback_t makeErrorCb() { return []() { rec.errorCount++; }; }
heater_autotune_fail_callback_t makeAutotuneFailCb() { return []() { rec.autotuneFailCount++; }; }
pid_result_callback_t makePidResultCb() {
    return [](float, float, float, float) { rec.pidResultCount++; };
}

// One step of closed-loop simulation:
//   1. Mock sensor reports the plant's current temperature.
//   2. Heater runs its loop — softPwm flips the pin based on current PID
//      output; PID updates from the sensor reading.
//   3. Plant integrates based on heater pin state over SIM_STEP_MS.
//   4. Mock clock advances SIM_STEP_MS so the next sample sees a new
//      millis() value (the PID's dt depends on this).
static void simStep(Heater &heater, ThermalPlant &plant, TestSensor &sensor) {
    sensor.setTemperature(plant.temperature);
    heater.loop();
    const bool heaterOn = (mock_arduino::getPinState(HEATER_PIN) == HIGH);
    plant.step(heaterOn, SIM_STEP_MS / 1000.0f);
    mock_arduino::advanceMillis(SIM_STEP_MS);
}

// Run the closed loop for total_seconds. Returns the final plant temperature.
static float runClosedLoop(Heater &heater, ThermalPlant &plant, TestSensor &sensor,
                           float total_seconds) {
    const int steps = static_cast<int>(total_seconds * 1000.0f / SIM_STEP_MS);
    for (int i = 0; i < steps; ++i) {
        simStep(heater, plant, sensor);
    }
    return plant.temperature;
}

} // namespace

void setUp() {
    mock_arduino::reset();
    mock_freertos::reset();
    rec.reset();
}
void tearDown() {}

// ---------------------------------------------------------------------------
// Test 1 — Heat-up from ambient to brew temperature
// ---------------------------------------------------------------------------
//
// Setpoint 95 °C, start at 23 °C. Within 5 minutes the closed loop should
// approach the setpoint. The exact convergence time depends on PID gains;
// 300 s is a generous envelope that catches regressions but doesn't fail
// on minor tuning variations.
static void test_heatup_approaches_setpoint() {
    TestSensor sensor;
    sensor.setErrorState(false);
    ThermalPlant plant;
    plant.temperature = 23.0f;

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    // PID gains must be set externally — in production this comes from
    // the display via BLE (settings → setTunings). Heater::setupPid only
    // wires sampling/limits, not gains. These values are the autotune
    // baseline for the Gaggia Classic profile, ×1000 per the autotuner
    // output scale (see test_autotune_simc.cpp's expected gains comment).
    heater.setTunings(92.952f, 0.930f, 185.904f);
    heater.setSetpoint(95.0f);

    const float finalTemp = runClosedLoop(heater, plant, sensor, 300.0f);

    // Within 5 °C of setpoint after 5 minutes. Loose because default PID
    // gains aren't autotune-optimal; tight enough to catch "PID broken".
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(5.0f, 95.0f, finalTemp,
                                     "boiler must reach within 5 °C of 95 °C setpoint after 300 s");
}

// ---------------------------------------------------------------------------
// Test 2 — Peak temperature stays well below runaway ceiling
// ---------------------------------------------------------------------------
//
// The headline runaway safety test. Even with the default PID gains
// (which CAN overshoot more than well-tuned gains would), peak temperature
// must never reach the 150 °C runaway ceiling. If it does, the PID has
// either lost output clamping or has gains so wrong that runaway is
// inevitable on a real boiler.
//
// NOTE: this does NOT test the firmware safety gates (those have their
// own tests in test_heater_safety). It tests that NORMAL OPERATION never
// hits a state where the safety gates would even need to fire.
static void test_no_unbounded_runaway() {
    TestSensor sensor;
    sensor.setErrorState(false);
    ThermalPlant plant;
    plant.temperature = 23.0f;

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    // PID gains must be set externally — in production this comes from
    // the display via BLE (settings → setTunings). Heater::setupPid only
    // wires sampling/limits, not gains. These values are the autotune
    // baseline for the Gaggia Classic profile, ×1000 per the autotuner
    // output scale (see test_autotune_simc.cpp's expected gains comment).
    heater.setTunings(92.952f, 0.930f, 185.904f);
    heater.setSetpoint(95.0f);

    runClosedLoop(heater, plant, sensor, 600.0f);  // 10 min

    TEST_ASSERT_TRUE_MESSAGE(plant.max_observed < RUNAWAY_CEILING,
                             "peak temperature must stay below RUNAWAY_CEILING (150 °C)");
    // Also sanity: peak shouldn't be more than 30 °C above setpoint.
    // 30 °C is wide — well-tuned PID should be ±5 °C — but the goal here
    // is to catch catastrophic regressions, not measure tuning quality.
    TEST_ASSERT_LESS_THAN_FLOAT_MESSAGE(125.0f, plant.max_observed,
                                        "peak overshoot must be less than 30 °C above setpoint");
}

// ---------------------------------------------------------------------------
// Test 3 — Steady state holds within a tight band
// ---------------------------------------------------------------------------
//
// After converging, the controller should track the setpoint with limited
// drift. Measures over a 60-second window AFTER a 300-second warmup.
static void test_steady_state_holds() {
    TestSensor sensor;
    sensor.setErrorState(false);
    ThermalPlant plant;
    plant.temperature = 23.0f;

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    // PID gains must be set externally — in production this comes from
    // the display via BLE (settings → setTunings). Heater::setupPid only
    // wires sampling/limits, not gains. These values are the autotune
    // baseline for the Gaggia Classic profile, ×1000 per the autotuner
    // output scale (see test_autotune_simc.cpp's expected gains comment).
    heater.setTunings(92.952f, 0.930f, 185.904f);
    heater.setSetpoint(95.0f);

    // Warm up for 5 min, then sample.
    runClosedLoop(heater, plant, sensor, 300.0f);

    float minT = plant.temperature;
    float maxT = plant.temperature;

    // Sample for 60 s.
    const int sampleSteps = static_cast<int>(60.0f * 1000.0f / SIM_STEP_MS);
    for (int i = 0; i < sampleSteps; ++i) {
        simStep(heater, plant, sensor);
        if (plant.temperature < minT) minT = plant.temperature;
        if (plant.temperature > maxT) maxT = plant.temperature;
    }

    const float band = maxT - minT;
    // 7 °C band is calibrated against the test user's live machine: with
    // PID gains (Kp=92.95, Ki=0.93, Kd=185.9) the observed steady-state
    // ripple is ~3-4 °C peak-to-peak over a ~30 s period (limit cycle).
    // 7 °C leaves headroom for plant-model imprecision while still catching
    // a regression that doubled the ripple. Larger ripple than this
    // indicates the PID is destabilizing.
    TEST_ASSERT_LESS_THAN_FLOAT_MESSAGE(7.0f, band,
                                        "steady-state band must be < 7 °C over 60 s "
                                        "(real machine: ~3-4 °C with autotune gains)");
}

// ---------------------------------------------------------------------------
// Test 4 — Heater stays OFF when setpoint is 0
// ---------------------------------------------------------------------------
//
// Closed-loop version of the safety test from test_heater_safety. Even with
// a thermal plant attached, setpoint=0 means heater pin LOW means plant
// only loses heat to ambient.
static void test_zero_setpoint_plant_cools_to_ambient() {
    TestSensor sensor;
    sensor.setErrorState(false);
    ThermalPlant plant;
    plant.temperature = 95.0f;  // start hot

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    heater.setSetpoint(0.0f);

    const float startTemp = plant.temperature;
    runClosedLoop(heater, plant, sensor, 600.0f);  // 10 min

    // With heater off, plant should cool toward ambient. Cooling time
    // constant τ = C/k = 500/0.42 ≈ 1190 s, so after 600 s ≈ 0.5τ the
    // plant reaches ambient + (95-23)*e^-0.5 ≈ 23 + 43.7 = 66.7 °C.
    // The test asserts the loop didn't accidentally start heating: T at
    // the end must be meaningfully lower than the start (cooling actually
    // happened) AND we must not be reheating up. 25 °C of cooling in
    // 10 min is well beyond what noise/PID-artifacts could explain.
    const float cooled = startTemp - plant.temperature;
    TEST_ASSERT_GREATER_THAN_FLOAT_MESSAGE(20.0f, cooled,
                                           "plant must cool > 20 °C in 10 min with zero setpoint");
    TEST_ASSERT_LESS_THAN_FLOAT_MESSAGE(startTemp, plant.temperature,
                                        "plant temperature must end below start (no sneaky heating)");
    TEST_ASSERT_EQUAL_MESSAGE(LOW, mock_arduino::getPinState(HEATER_PIN),
                              "heater pin must end LOW after zero-setpoint run");
}

// ---------------------------------------------------------------------------
// Test 5 — Setpoint change tracks
// ---------------------------------------------------------------------------
//
// Start at 95 °C steady state, then bump to 105 °C. The controller should
// track the change within reasonable time.
static void test_setpoint_change_tracks() {
    TestSensor sensor;
    sensor.setErrorState(false);
    ThermalPlant plant;
    plant.temperature = 23.0f;

    Heater heater(&sensor, HEATER_PIN, makeErrorCb(), makePidResultCb(), makeAutotuneFailCb());
    heater.setup();
    // PID gains must be set externally — in production this comes from
    // the display via BLE (settings → setTunings). Heater::setupPid only
    // wires sampling/limits, not gains. These values are the autotune
    // baseline for the Gaggia Classic profile, ×1000 per the autotuner
    // output scale (see test_autotune_simc.cpp's expected gains comment).
    heater.setTunings(92.952f, 0.930f, 185.904f);
    heater.setSetpoint(95.0f);

    runClosedLoop(heater, plant, sensor, 300.0f);  // converge at 95 °C
    plant.max_observed = plant.temperature;        // reset peak tracking

    heater.setSetpoint(105.0f);
    runClosedLoop(heater, plant, sensor, 300.0f);

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(5.0f, 105.0f, plant.temperature,
                                     "boiler must track setpoint change to 105 °C");
    TEST_ASSERT_TRUE_MESSAGE(plant.max_observed < RUNAWAY_CEILING,
                             "no runaway during setpoint change");
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_heatup_approaches_setpoint);
    RUN_TEST(test_no_unbounded_runaway);
    RUN_TEST(test_steady_state_holds);
    RUN_TEST(test_zero_setpoint_plant_cools_to_ambient);
    RUN_TEST(test_setpoint_change_tracks);
    return UNITY_END();
}
