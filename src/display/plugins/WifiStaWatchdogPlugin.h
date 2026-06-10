#ifndef WIFISTAWATCHDOGPLUGIN_H
#define WIFISTAWATCHDOGPLUGIN_H

#include "../core/Plugin.h"
#include <Arduino.h>

struct Event;

// Reassoc when SDK auto-reconnect gives up (reason codes outside _is_staReconnectableReason).
// Never reboots: a mid-outage reboot lands in AP-fallback, which has no STA retry.
class WifiStaWatchdogPlugin : public Plugin {
  public:
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;

  private:
    static constexpr unsigned long STA_DOWN_GRACE_MS = 20000;
    static constexpr unsigned long STA_REASSOC_BACKOFF_MS = 30000;
    static constexpr unsigned int MAX_FIRES_BEFORE_STACK_RESTART = 10; // 20s + 10*30s = ~5min 20s down

    Controller *controller = nullptr;
    String ssid;
    String pass;
    unsigned long lastConnectedMs = 0;
    unsigned long lastReassocMs = 0;
    unsigned int consecutiveFires = 0;
    bool armed = false;
    bool updating = false;

    void forceReassoc();
    void maybeRestartStack();
};

#endif // WIFISTAWATCHDOGPLUGIN_H
