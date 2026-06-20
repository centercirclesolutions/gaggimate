// ESP-IDF log macros — no-op for host-native tests.
//
// Production builds compile these to actual log calls. In tests we don't
// validate log output (the safety tests focus on pin state + callback
// invocation), so we collapse all log levels to nothing.

#pragma once
#ifndef NATIVE_MOCKS_ESP_LOG_H
#define NATIVE_MOCKS_ESP_LOG_H

#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)

#define ESP_LOG_LEVEL(level, tag, fmt, ...) ((void)0)

#endif // NATIVE_MOCKS_ESP_LOG_H
