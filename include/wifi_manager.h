#pragma once

#include <stdint.h>

struct WifiCredentials {
  char ssid[33] = "";
  char password[65] = "";
  char identity[65] = "";
  char username[65] = "";
};

// Saved networks, persisted as one NVS blob along with the other settings.
constexpr uint8_t kMaxWifiProfiles = 6;

struct WifiBook {
  using Profile = WifiCredentials;
  uint8_t version = 1;
  uint8_t count = 0;
  uint8_t active = 0;
  WifiCredentials list[kMaxWifiProfiles];
};

#if defined(USE_WIFI)

struct WifiNetwork {
  char ssid[33];
  int16_t rssi;
};

void initWifi();
void updateWifi(uint32_t now);
void startWifiScan();
bool wifiScanRunning();
uint8_t wifiScanCount();
const WifiNetwork* wifiScanResults();
const WifiNetwork* wifiScanResult(uint8_t index);
bool wifiConnect(const WifiBook& book);
bool wifiConnect(const WifiCredentials& credentials);
// Try saved networks in order; skips profiles without usable credentials.
bool wifiAutoConnect(const WifiBook& book);
const char* wifiStatusText();
bool isWifiConnected();

#endif  // USE_WIFI
