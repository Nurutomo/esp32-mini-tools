#include "navigation_state.h"
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <string.h>

namespace {
NavigationData gNavData;

// Clock state: epoch seconds from phone + millis() snapshot at sync time.
// The ESP keeps its own time by adding elapsed millis since last sync.
uint64_t gClockEpochSec = 0;   // Unix timestamp at last sync (seconds)
uint32_t gClockSyncMs   = 0;   // millis() value when that sync arrived
bool     gClockSynced   = false;

// Return current Unix epoch seconds (best-effort, drifts without periodic resync).
uint64_t currentEpochSec() {
  if (!gClockSynced) return 0;
  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - gClockSyncMs;  // wraps after ~49 days, acceptable
  return gClockEpochSec + (elapsedMs / 1000UL);
}

// Convert epoch seconds to broken-down time components (UTC only; no DST/timezone).
// For display the phone already sends wall-clock epoch, so the user sees local time.
struct ClockFields { uint8_t h, m, d, mo; };
ClockFields epochToFields(uint64_t epoch) {
  // Days since Unix epoch
  uint32_t days = static_cast<uint32_t>(epoch / 86400UL);
  uint32_t rem  = static_cast<uint32_t>(epoch % 86400UL);
  uint8_t h = rem / 3600;
  uint8_t m = (rem % 3600) / 60;

  // Gregorian calendar calculation
  uint32_t z = days + 719468;
  uint32_t era = z / 146097;
  uint32_t doe = z - era * 146097;
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp  = (5 * doy + 2) / 153;
  uint8_t  d   = static_cast<uint8_t>(doy - (153 * mp + 2) / 5 + 1);
  uint8_t  mo  = static_cast<uint8_t>(mp < 10 ? mp + 3 : mp - 9);
  return {h, m, d, mo};
}
} // namespace

NavigationData& getNavigationData() {
  return gNavData;
}

bool processNavigationJson(const char* payload, size_t len) {
  if (len == 0 || payload == nullptr) return false;

  // Quick sanity check for JSON object
  if (payload[0] != '{') return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) return false;

  bool recognized = false;
  gNavData.lastUpdateMs = millis();

  // 1. Navigation block
  if (doc["navigation"].is<JsonObject>()) {
    JsonObject nav = doc["navigation"];
    recognized = true;

    const char* road = nav["next_road"] | "";
    const char* sub = nav["next_road_sub"] | "";
    const char* dist = nav["next_road_distance"] | "";
    const char* eta = nav["eta"] | "";
    const char* ete = nav["ete"] | "";
    const char* totDist = nav["distance"] | "";
    const char* iconB64 = nav["icon"] | "";

    // Trigger flash indicator if maneuver or road changes
    if (strlen(road) > 0 && strcmp(gNavData.nextRoad, road) != 0) {
      gNavData.newTurnManeuver = true;
    }

    strncpy(gNavData.nextRoad, road, sizeof(gNavData.nextRoad) - 1);
    gNavData.nextRoad[sizeof(gNavData.nextRoad) - 1] = '\0';

    strncpy(gNavData.nextRoadSub, sub, sizeof(gNavData.nextRoadSub) - 1);
    gNavData.nextRoadSub[sizeof(gNavData.nextRoadSub) - 1] = '\0';

    strncpy(gNavData.distance, dist, sizeof(gNavData.distance) - 1);
    gNavData.distance[sizeof(gNavData.distance) - 1] = '\0';

    strncpy(gNavData.eta, eta, sizeof(gNavData.eta) - 1);
    gNavData.eta[sizeof(gNavData.eta) - 1] = '\0';

    strncpy(gNavData.ete, ete, sizeof(gNavData.ete) - 1);
    gNavData.ete[sizeof(gNavData.ete) - 1] = '\0';

    strncpy(gNavData.totalDistance, totDist, sizeof(gNavData.totalDistance) - 1);
    gNavData.totalDistance[sizeof(gNavData.totalDistance) - 1] = '\0';

    if (strlen(iconB64) > 0) {
      size_t olen = 0;
      int ret = mbedtls_base64_decode(gNavData.iconBitmap, sizeof(gNavData.iconBitmap),
                                      &olen, (const unsigned char*)iconB64, strlen(iconB64));
      gNavData.hasIcon = (ret == 0 && olen == sizeof(gNavData.iconBitmap));
    } else {
      gNavData.hasIcon = false;
    }

    gNavData.active = (strlen(gNavData.nextRoad) > 0 || strlen(gNavData.distance) > 0);
  }

  // 2. Speed telemetry from GPS
  if (doc["speed"].is<int>()) {
    gNavData.speed = doc["speed"].as<int>();
    recognized = true;
  }

  // 3. User preferences
  if (doc["preferences"].is<JsonObject>()) {
    JsonObject prefs = doc["preferences"];
    if (prefs["display_backlight"].is<bool>()) {
      gNavData.displayBacklight = prefs["display_backlight"].as<bool>();
    }
    if (prefs["speed_limit"].is<int>()) {
      gNavData.speedLimit = prefs["speed_limit"].as<int>();
    }
    recognized = true;
  }

  // 4. Phone clock sync — Unix epoch seconds (local time from phone)
  if (doc["ts"].is<uint64_t>() || doc["ts"].is<long>()) {
    const uint64_t ts = doc["ts"].as<uint64_t>();
    if (ts > 1000000000ULL) {  // sanity: must be after year 2001
      gClockEpochSec = ts;
      gClockSyncMs   = millis();
      gClockSynced   = true;
      recognized = true;
    }
  }

  return recognized;
}

bool isNavigationActive(uint32_t timeoutMs) {
  if (!gNavData.active) return false;
  return (millis() - gNavData.lastUpdateMs) < timeoutMs;
}

void resetNavigation() {
  gNavData.active = false;
  gNavData.nextRoad[0] = '\0';
  gNavData.nextRoadSub[0] = '\0';
  gNavData.distance[0] = '\0';
  gNavData.eta[0] = '\0';
  gNavData.ete[0] = '\0';
  gNavData.totalDistance[0] = '\0';
  gNavData.speed = -1;
  gNavData.hasIcon = false;
  memset(gNavData.iconBitmap, 0, sizeof(gNavData.iconBitmap));
}

bool isClockSynced() {
  return gClockSynced;
}

void getClockTimeStr(char* buf, size_t bufLen) {
  if (!gClockSynced) {
    strncpy(buf, "--:--", bufLen);
    buf[bufLen - 1] = '\0';
    return;
  }
  const ClockFields f = epochToFields(currentEpochSec());
  snprintf(buf, bufLen, "%02u:%02u", f.h, f.m);
}

void getClockDateStr(char* buf, size_t bufLen) {
  if (!gClockSynced) {
    strncpy(buf, "--/--", bufLen);
    buf[bufLen - 1] = '\0';
    return;
  }
  const ClockFields f = epochToFields(currentEpochSec());
  snprintf(buf, bufLen, "%02u/%02u", f.d, f.mo);
}
