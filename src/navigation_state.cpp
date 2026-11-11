#include "navigation_state.h"
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <string.h>

namespace {
NavigationData gNavData;
}

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
