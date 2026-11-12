#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

constexpr size_t kIconWidth = 32;
constexpr size_t kIconHeight = 32;
constexpr size_t kIconBufferSize = (kIconWidth * kIconHeight) / 8; // 128 bytes
constexpr uint32_t kNavTimeoutDefaultMs = 15000; // 15s without update falls back to status

struct NavigationData {
  bool active = false;
  uint32_t lastUpdateMs = 0;
  char nextRoad[64] = "";
  char nextRoadSub[64] = "";
  char distance[32] = "";
  char eta[32] = "";
  char ete[32] = "";
  char totalDistance[32] = "";
  int speed = -1;
  bool hasIcon = false;
  uint8_t iconBitmap[kIconBufferSize] = {0};

  // User preferences from phone
  bool displayBacklight = false;
  int speedLimit = 60;
  bool newTurnManeuver = false; // Flag set on turn change to trigger screen flash
};

// Access the global navigation state
NavigationData& getNavigationData();

// Parse incoming JSON payload (returns true if valid navigation/speed JSON)
bool processNavigationJson(const char* payload, size_t len);

// Check if navigation data is currently active and fresh
bool isNavigationActive(uint32_t timeoutMs = kNavTimeoutDefaultMs);

// Clear navigation state
void resetNavigation();

// Clock synced from phone. Returns true if a time sync has been received.
bool isClockSynced();

// Fill buf with current time "HH:MM" (up to bufLen bytes, NUL-terminated).
void getClockTimeStr(char* buf, size_t bufLen);

// Fill buf with current date "DD/MM" (up to bufLen bytes, NUL-terminated).
void getClockDateStr(char* buf, size_t bufLen);
