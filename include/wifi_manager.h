#pragma once

#include <stdint.h>

struct WifiCredentials {
  char ssid[33] = "";
  char password[65] = "";
  char identity[65] = "";
  char username[65] = "";
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
bool wifiConnect(const WifiCredentials& credentials);
const char* wifiStatusText();
bool isWifiConnected();

#endif  // USE_WIFI
