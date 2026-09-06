#pragma once

#ifdef POCKET_POKER_DESKTOP
constexpr int WIFI_OFF = 0;
class DesktopWiFi {
public:
    void persistent(bool) {}
    void mode(int) {}
};
extern DesktopWiFi WiFi;
#endif
