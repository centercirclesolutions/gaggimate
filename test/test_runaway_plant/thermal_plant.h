// Thermal plant simulator for closed-loop heater PID testing.
//
// Single-mass first-order model — captures the dominant dynamics of an
// espresso boiler for control-quality regression tests:
//
//   dT/dt = (P_in - P_out) / C
//   P_in  = heater_on ? wattage : 0          (heater is binary via softPwm)
//   P_out = k_loss * (T - T_ambient)
//
// Default values calibrated to a Gaggia Classic boiler ballpark:
//   wattage       = 680 W   (E24 / 230 V immersion)
//   thermal_mass  = 120 J/°C (~300 g brass + water inertia)
//   k_loss        = 0.5 W/°C (~well-insulated)
//   T_ambient     = 23 °C
//
// Steady state at 95 °C ≈ 0.5 * (95-23) / 680 ≈ 5.3 % duty. Time constant
// τ = C / k = 240 s for cooling. Heat-up at full power: dT/dt = 680/120 =
// 5.67 °C/s at T=ambient, dropping as T approaches steady state.
//
// The model is intentionally not high-fidelity — it doesn't capture
// thermosyphoning, boiler bowl thermal inertia separately, or steam-state
// nonlinearities. It captures enough to make PID quality testable:
// "does the controller converge without unbounded overshoot?".

#pragma once
#ifndef TEST_RUNAWAY_PLANT_THERMAL_PLANT_H
#define TEST_RUNAWAY_PLANT_THERMAL_PLANT_H

struct ThermalPlant {
    float temperature = 23.0f;       // current boiler temp °C
    float ambient = 23.0f;           // °C
    // 1370 W — Gaggia Classic Pro E24 brass boiler embedded element on
    // 110-120 V (per Gaggia NA + WholeLattileLove specs). Older Classic
    // Pro variants are 570/680 W; the E24 (2024+, brass boiler) is the
    // one the test user has.
    float wattage = 1370.0f;
    // Calibrated against the test user's real machine data
    // (Temperature & Pressure history charts at cold-start + standby):
    //
    // Heat-up bulk rate observed = 1.38 °C/s. C = wattage / rate
    //   = 1370 / 1.38 ≈ 993. Round to 1000. Matches a brass-boiler-plus-
    //   109 ml water mass at typical specific heats.
    //
    // Cooldown (heater off, observed in 3 standby charts):
    //   Early phase (T≈88 °C, ΔT_amb ≈ 65 °C): dT/dt ≈ -0.15 °C/s
    //     → k = 0.15·1000/65 ≈ 2.31 W/°C
    //   Late phase  (T≈78 °C, ΔT_amb ≈ 55 °C): dT/dt ≈ -0.037 °C/s
    //     → k = 0.037·1000/55 ≈ 0.67 W/°C
    //
    // Geometric mean of the two extremes ≈ 1.24 W/°C. The factor-of-~3
    // variation is the real boiler having TWO thermal masses (shell +
    // water) that cool at different rates — outside the first-order
    // model. 1.24 W/°C is close to early-phase truth (most relevant for
    // safety-test "is the boiler cooling at all" semantics), acceptable
    // for late-phase. τ = C/k ≈ 806 s for the cooldown time constant.
    float thermal_mass = 1000.0f;    // J/°C
    float k_loss = 1.24f;            // W/°C
    float max_observed = 23.0f;      // peak temperature seen (for overshoot/runaway assertions)

    void step(bool heater_on, float dt_sec) {
        const float p_in = heater_on ? wattage : 0.0f;
        const float p_out = k_loss * (temperature - ambient);
        temperature += (p_in - p_out) * dt_sec / thermal_mass;
        if (temperature > max_observed) max_observed = temperature;
    }
};

#endif // TEST_RUNAWAY_PLANT_THERMAL_PLANT_H
