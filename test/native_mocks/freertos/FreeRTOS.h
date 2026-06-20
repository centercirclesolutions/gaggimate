// Minimal FreeRTOS header mock for host-native tests.
//
// Real FreeRTOS provides TaskHandle_t, TickType_t, and the scheduling APIs.
// Our test harness only needs the type names; the tests drive Heater::loop()
// directly without actually scheduling tasks. xTaskCreate stores the task
// function and arg for inspection but does NOT spawn a thread.

#pragma once
#ifndef NATIVE_MOCKS_FREERTOS_H
#define NATIVE_MOCKS_FREERTOS_H

#include <cstdint>

using TaskHandle_t = void *;
using xTaskHandle = TaskHandle_t; // legacy typedef (pre-Core-3.x)
using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned int;

enum eTaskState { eRunning = 0, eReady, eBlocked, eSuspended, eDeleted, eInvalid };

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS pdTRUE

// configMINIMAL_STACK_SIZE is in 32-bit words on FreeRTOS, ~768 by default.
// We don't actually allocate stack in tests; this is just a referenced macro.
#define configMINIMAL_STACK_SIZE 768

#define portTICK_PERIOD_MS 1

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#endif // NATIVE_MOCKS_FREERTOS_H
