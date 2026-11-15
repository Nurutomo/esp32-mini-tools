#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

constexpr size_t kIconWidth = 32;
constexpr size_t kIconHeight = 32;
constexpr size_t kIconBufferSize = (kIconWidth * kIconHeight) / 8; // 128 bytes
constexpr uint32_t kNavTimeoutDefaultMs = 15000; // 15s without update falls back to status

// Compact binary frame: [0xAA 0x55][type][payload len LE16][payload]
// type 0x01 = raw navigation payload, type 0x02 = canonical Huffman encoded
// ([uncompressed len LE16][bitstream]) whose decoded bytes are a type 0x01 payload.
constexpr uint8_t kNavBinMagic0 = 0xAA;
constexpr uint8_t kNavBinMagic1 = 0x55;
constexpr uint8_t kNavBinTypeNav = 0x01;
constexpr uint8_t kNavBinTypeHuffman = 0x02;
// Payload flag: icon bytes are row-differenced (each row XOR the row above).
// Monochrome maneuver icons are mostly empty rows, so this makes the bitmap
// compress about 40% better through the shared Huffman codebook.
constexpr uint8_t kNavBinFlagIconRowDiff = 0x08;
constexpr size_t kNavBinHeaderSize = 5;
// Raw payload ceiling; the Huffman path can exceed it on incompressible input.
constexpr size_t kNavBinMaxContent = 512;
// Wire payload ceiling: must keep kNavBinHeaderSize + payload within the
// Bluetooth manager's kMaxPacketBytes (1024) frame buffer.
constexpr size_t kNavBinMaxPayload = 1019;

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

// Parse incoming compact binary navigation frame (returns true if valid)
bool processNavigationBinary(const uint8_t* payload, size_t len);

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
