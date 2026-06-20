// Minimal FreeRTOS task.h mock.
//
// Provides xTaskCreate (stores task fn + arg without spawning a thread),
// vTaskDelay (advances the mock clock), xTaskDelayUntil (no-op), and
// xTaskGetTickCount (returns mock millis as ticks since configHZ=1000 on
// real hardware).

#pragma once
#ifndef NATIVE_MOCKS_FREERTOS_TASK_H
#define NATIVE_MOCKS_FREERTOS_TASK_H

#include "FreeRTOS.h"
// Note: this header is intentionally included AT THE END of native_mocks/
// Arduino.h, after mock_arduino is declared. Including it standalone (e.g.
// directly from a peripheral .h that uses #include <freertos/task.h>) will
// fail to find mock_arduino unless Arduino.h has already been parsed.
// In practice every production peripheral that uses task.h also uses
// Arduino.h, so the ordering works out.

namespace mock_freertos {
// Records the most recent xTaskCreate so tests can verify (or inspect) what
// task spawn was attempted. The test does NOT execute the task body — it
// calls heater->loop() directly to bypass scheduling.
struct CreatedTask {
    void (*fn)(void *) = nullptr;
    const char *name = nullptr;
    void *arg = nullptr;
    UBaseType_t priority = 0;
    TaskHandle_t *handle = nullptr;
};

inline CreatedTask lastCreatedTask;

inline void reset() { lastCreatedTask = CreatedTask{}; }
} // namespace mock_freertos

inline BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t /*stackDepth*/, void *arg,
                              UBaseType_t prio, TaskHandle_t *handle) {
    mock_freertos::lastCreatedTask = {fn, name, arg, prio, handle};
    if (handle) *handle = reinterpret_cast<TaskHandle_t>(0x1); // non-null sentinel
    return pdPASS;
}

inline BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, uint32_t stack, void *arg,
                                          UBaseType_t prio, TaskHandle_t *handle, BaseType_t /*core*/) {
    return xTaskCreate(fn, name, stack, arg, prio, handle);
}

inline void vTaskDelay(TickType_t ticks) { mock_arduino::advanceMillis(ticks); }

inline TickType_t xTaskGetTickCount() {
    // Real FreeRTOS at configHZ=1000 → 1 tick = 1 ms — same scale as our mock.
    return static_cast<TickType_t>(mock_arduino::mockMillis);
}

inline void xTaskDelayUntil(TickType_t *lastWakeTime, TickType_t increment) {
    if (lastWakeTime) {
        *lastWakeTime += increment;
        mock_arduino::setMillis(static_cast<uint64_t>(*lastWakeTime));
    } else {
        mock_arduino::advanceMillis(static_cast<uint64_t>(increment));
    }
}

inline void vTaskDelete(TaskHandle_t /*handle*/) {}
inline void vTaskSuspend(TaskHandle_t /*handle*/) {}
inline void vTaskResume(TaskHandle_t /*handle*/) {}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t /*handle*/) { return 1024; }

#endif // NATIVE_MOCKS_FREERTOS_TASK_H
