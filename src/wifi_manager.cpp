#include "wifi_manager.h"

#if defined(USE_WIFI)

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#include "ui/settings_model.h"

namespace {

constexpr uint8_t kMaxScanResults = 12;
// Bounded so a dead AP or an unreachable RADIUS server cannot pin "Connecting".
constexpr uint32_t kConnectTimeoutMs = 20000;

WifiNetwork gNetworks[kMaxScanResults];
uint8_t gNetworkCount = 0;
bool gScanRunning = false;
bool gConnecting = false;
uint32_t gConnectStartedMs = 0;
bool gConnectFailed = false;
WifiBook gAutoBook;
bool gAutoBookValid = false;
bool gAutoConnecting = false;
uint8_t gAutoStart = 0;
uint8_t gAutoTried = 0;
bool gAutoExhausted = false;

bool connectProfile(const WifiCredentials& credentials, uint32_t now) {
  if (credentials.ssid[0] == '\0') return false;
  const auto security = ui::settings::model::classifySsid(credentials.ssid);
  if (!ui::settings::model::credentialsComplete(security, credentials.password,
                                                credentials.identity,
                                                credentials.username)) return false;

  WiFi.disconnect(true);
  if (security == ui::settings::model::Security::Eduroam) {
#if CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT
    // ponytail: no CA certificate, so the RADIUS server is not validated. Add a
    // CA field when a campus that requires it shows up.
    WiFi.begin(credentials.ssid, WPA2_AUTH_PEAP, credentials.identity,
               credentials.username, credentials.password);
#else
    return false;  // Enterprise support is not compiled into this core.
#endif
  } else {
    WiFi.begin(credentials.ssid, credentials.password);
  }
  gConnecting = true;
  gConnectFailed = false;
  gConnectStartedMs = now;
  return true;
}

bool tryNextAutoProfile(uint32_t now) {
  while (gAutoBookValid && gAutoTried < gAutoBook.count &&
         gAutoTried < kMaxWifiProfiles) {
    const uint8_t index = static_cast<uint8_t>((gAutoStart + gAutoTried) % gAutoBook.count);
    ++gAutoTried;
    gAutoBook.active = index;
    if (connectProfile(gAutoBook.list[index], now)) return true;
  }
  gAutoConnecting = false;
  gAutoExhausted = true;
  gConnecting = false;
  gConnectFailed = true;
  return false;
}

}  // namespace

void initWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
}

void startWifiScan() {
  if (gScanRunning) return;
  gNetworkCount = 0;
  gScanRunning = true;
  WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
}

bool wifiScanRunning() { return gScanRunning; }

uint8_t wifiScanCount() { return gNetworkCount; }

const WifiNetwork* wifiScanResults() { return gNetworks; }

const WifiNetwork* wifiScanResult(uint8_t index) {
  return index < gNetworkCount ? &gNetworks[index] : nullptr;
}

void updateWifi(uint32_t now) {
  if (gScanRunning) {
    const int16_t found = WiFi.scanComplete();
    if (found >= 0) {
      gNetworkCount = 0;
      for (int16_t i = 0; i < found && gNetworkCount < kMaxScanResults; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;
        WifiNetwork& network = gNetworks[gNetworkCount++];
        strncpy(network.ssid, ssid.c_str(), sizeof(network.ssid) - 1);
        network.ssid[sizeof(network.ssid) - 1] = '\0';
        network.rssi = static_cast<int16_t>(WiFi.RSSI(i));
      }
      WiFi.scanDelete();
      gScanRunning = false;
    } else if (found == WIFI_SCAN_FAILED) {
      gNetworkCount = 0;
      gScanRunning = false;
    }
  }

  if (gConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      gConnecting = false;
      gAutoConnecting = false;
      gAutoExhausted = false;
      gConnectFailed = false;
    } else if (now - gConnectStartedMs >= kConnectTimeoutMs) {
      gConnecting = false;
      WiFi.disconnect();
      if (!gAutoConnecting || !tryNextAutoProfile(now)) gConnectFailed = true;
    }
  }
}

bool wifiConnect(const WifiCredentials& credentials) {
  gAutoConnecting = false;
  gAutoExhausted = false;
  gAutoBookValid = false;
  return connectProfile(credentials, millis());
}

bool wifiConnect(const WifiBook& book) {
  if (!ui::settings::model::profiles::valid(book) || book.count == 0) return false;
  return wifiConnect(book.list[book.active]);
}

bool wifiAutoConnect(const WifiBook& book) {
  if (!ui::settings::model::profiles::valid(book) || book.count == 0) return false;
  gAutoBook = book;
  gAutoBookValid = true;
  gAutoStart = static_cast<uint8_t>(book.active % book.count);
  gAutoBook.active = gAutoStart;
  gAutoExhausted = false;
  gAutoConnecting = true;
  gAutoTried = 0;
  gConnectFailed = false;
  return tryNextAutoProfile(millis());
}

const char* wifiStatusText() {
  if (WiFi.status() == WL_CONNECTED) return "Connected";
  if (gConnecting) return "Connecting...";
  if (gConnectFailed) return "Connect failed";
  return "Not connected";
}

bool isWifiConnected() { return WiFi.status() == WL_CONNECTED; }

#endif  // USE_WIFI
