#include "wifi_manager.h"

#if defined(USE_WIFI)

#include <Arduino.h>
#include <WiFi.h>

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

}  // namespace

void initWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
}

void startWifiScan() {
  if (gScanRunning) return;
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
        if (ssid.isEmpty()) continue;  // hidden/unnamed APs cannot be selected
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
      gConnectFailed = false;
    } else if (now - gConnectStartedMs >= kConnectTimeoutMs) {
      gConnecting = false;
      gConnectFailed = true;
      WiFi.disconnect();
    }
  }
}

bool wifiConnect(const WifiCredentials& credentials) {
  if (credentials.ssid[0] == '\0') return false;
  const auto security = ui::settings::model::classifySsid(credentials.ssid);
  if (!ui::settings::model::credentialsComplete(security, credentials.password,
                                                credentials.identity,
                                                credentials.username)) {
    return false;
  }

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
  gConnectStartedMs = millis();
  return true;
}

const char* wifiStatusText() {
  if (WiFi.status() == WL_CONNECTED) return "Connected";
  if (gConnecting) return "Connecting...";
  if (gConnectFailed) return "Connect failed";
  return "Not connected";
}

bool isWifiConnected() { return WiFi.status() == WL_CONNECTED; }

#endif  // USE_WIFI
