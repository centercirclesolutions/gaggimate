#include "WifiStaWatchdogPlugin.h"
#include "../core/Controller.h"
#include "../core/Settings.h"
#include <WiFi.h>
#include <esp_log.h>
#include <esp_wifi.h>

static constexpr char LOG_TAG[] = "WifiStaWd";

void WifiStaWatchdogPlugin::setup(Controller *c, PluginManager *pluginManager) {
    controller = c;
    ssid = controller->getSettings().getWifiSsid();
    pass = controller->getSettings().getWifiPassword();
    armed = ssid.length() > 0 && pass.length() > 0;
    lastConnectedMs = millis();
    lastReassocMs = 0;
    consecutiveFires = 0;

    pluginManager->on("ota:update:start", [this](Event const &) { updating = true; });
    pluginManager->on("ota:update:end", [this](Event const &) { updating = false; });

    ESP_LOGI(LOG_TAG, "armed=%d grace=%lums backoff=%lums stack_restart_after=%u fires",
             armed, STA_DOWN_GRACE_MS, STA_REASSOC_BACKOFF_MS, MAX_FIRES_BEFORE_STACK_RESTART);
}

void WifiStaWatchdogPlugin::loop() {
    if (!armed || updating)
        return;

    // In AP-fallback there's no STA to recover.
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_NULL)
        return;

    const unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        lastConnectedMs = now;
        consecutiveFires = 0;
        return;
    }

    if (now - lastConnectedMs < STA_DOWN_GRACE_MS)
        return;
    if (lastReassocMs != 0 && now - lastReassocMs < STA_REASSOC_BACKOFF_MS)
        return;

    forceReassoc();
    lastReassocMs = now;
    ++consecutiveFires;
    maybeRestartStack();
}

void WifiStaWatchdogPlugin::forceReassoc() {
    wifi_ap_record_t ap{};
    const bool haveAp = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    ESP_LOGW(LOG_TAG, "STA down %lums; forcing reconnect (status=%d, fire %u/%u)",
             millis() - lastConnectedMs, (int)WiFi.status(),
             consecutiveFires + 1, MAX_FIRES_BEFORE_STACK_RESTART);
    if (haveAp) {
        ESP_LOGW(LOG_TAG, "  last AP: bssid=%02x:%02x:%02x:%02x:%02x:%02x rssi=%d ch=%u",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5],
                 ap.rssi, ap.primary);
    }

    WiFi.disconnect(false); // keep stored wifi_config_t
    delay(50);
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void WifiStaWatchdogPlugin::maybeRestartStack() {
    if (consecutiveFires < MAX_FIRES_BEFORE_STACK_RESTART)
        return;
    if (controller->isActive()) {
        ESP_LOGW(LOG_TAG, "%u fires without recovery; deferring stack restart while process active",
                 consecutiveFires);
        return;
    }
    ESP_LOGE(LOG_TAG, "%u fires without recovery; restarting WiFi driver", consecutiveFires);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), pass.c_str());
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    consecutiveFires = 0;
}
