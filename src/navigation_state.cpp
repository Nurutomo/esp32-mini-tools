#include "navigation_state.h"
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <string.h>

namespace {
NavigationData gNavData;

// Clock state: epoch seconds from phone + millis() snapshot at sync time.
// The ESP keeps its own time by adding elapsed millis since last sync.
uint64_t gClockEpochSec  = 0;   // Unix timestamp at last sync (seconds, UTC)
uint32_t gClockSyncMs    = 0;   // millis() value when that sync arrived
int32_t  gClockTzOffsetSec = 0; // UTC offset in seconds (e.g. +25200 for UTC+7)
bool     gClockSynced    = false;

// Return current local epoch seconds (UTC + tz offset).
uint64_t currentLocalEpochSec() {
  if (!gClockSynced) return 0;
  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - gClockSyncMs;
  return gClockEpochSec + (elapsedMs / 1000UL) + static_cast<uint32_t>(gClockTzOffsetSec);
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

uint16_t readU16Le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t readU32Le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Fixed canonical codebook shared with NavigationBinaryProtocol.kt.
constexpr uint8_t kHuffLengths[256] = {
    2, 15, 15, 15, 8, 6, 7, 7, 9, 7, 15, 15, 8, 15, 15, 9,
    9, 15, 15, 15, 15, 15, 15, 15, 7, 15, 15, 15, 15, 15, 15, 15,
    4, 15, 15, 15, 15, 15, 15, 15, 15, 15, 7, 15, 15, 15, 7, 15,
    6, 6, 6, 6, 8, 6, 15, 8, 8, 10, 7, 15, 7, 15, 15, 15,
    15, 9, 15, 15, 15, 15, 15, 9, 15, 8, 8, 15, 7, 9, 15, 15,
    9, 15, 8, 8, 9, 15, 15, 15, 15, 9, 15, 15, 15, 15, 15, 15,
    15, 6, 7, 15, 7, 6, 8, 8, 7, 6, 15, 7, 7, 5, 5, 6,
    7, 15, 7, 9, 6, 8, 15, 9, 9, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 2,
};

uint16_t kHuffFirstCode[16];
uint16_t kHuffFirstSymbol[16];
uint16_t kHuffCounts[16];
uint8_t kHuffSymbols[256];
bool gHuffTableReady = false;

bool initHuffmanTable() {
  if (gHuffTableReady) return true;
  uint16_t counts[16] = {};
  for (size_t symbol = 0; symbol < 256; ++symbol) {
    const uint8_t length = kHuffLengths[symbol];
    if (length == 0 || length > 15) return false;
    ++counts[length];
  }

  uint32_t code = 0;
  uint16_t symbolOffset = 0;
  for (uint8_t length = 1; length <= 15; ++length) {
    kHuffFirstCode[length] = static_cast<uint16_t>(code);
    kHuffFirstSymbol[length] = symbolOffset;
    kHuffCounts[length] = counts[length];
    for (size_t symbol = 0; symbol < 256; ++symbol) {
      if (kHuffLengths[symbol] == length) kHuffSymbols[symbolOffset++] = static_cast<uint8_t>(symbol);
    }
    code = (code + counts[length]) << 1;
  }
  // Kraft sum must be exactly 1: 256 symbols assigned and the tree left aligned.
  if (symbolOffset != 256 || code != (1UL << 16)) return false;
  gHuffTableReady = true;
  return true;
}

bool decodeHuffman(const uint8_t* data, size_t dataLen, uint8_t* output, size_t outputLen) {
  if (data == nullptr || output == nullptr || !initHuffmanTable()) return false;
  size_t bitPos = 0;
  for (size_t outPos = 0; outPos < outputLen; ++outPos) {
    uint16_t code = 0;
    bool found = false;
    for (uint8_t length = 1; length <= 15; ++length) {
      if (bitPos >= dataLen * 8) return false;
      code = static_cast<uint16_t>((code << 1) |
          ((data[bitPos >> 3] >> (7 - (bitPos & 7))) & 1));
      ++bitPos;
      const uint16_t first = kHuffFirstCode[length];
      if (code >= first && code - first < kHuffCounts[length]) {
        output[outPos] = kHuffSymbols[kHuffFirstSymbol[length] + code - first];
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  // Require exact byte count and zero-only pad bits, rejecting trailing data.
  const size_t usedBytes = (bitPos + 7) / 8;
  if (usedBytes != dataLen) return false;
  if ((bitPos & 7) != 0 && (data[dataLen - 1] & ((1U << (8 - (bitPos & 7))) - 1U)) != 0) return false;
  return true;
}

// Copy a length-prefixed field, always NUL-terminated. Returns false when the
// payload ran out before the prefix or its bytes.
bool readField(char* dst, size_t dstSize, const uint8_t** cursor, const uint8_t* end) {
  const uint8_t* p = *cursor;
  if (p >= end) return false;
  const size_t n = *p++;
  if (p + n > end) return false;
  const size_t copy = n < dstSize ? n : dstSize - 1;
  memcpy(dst, p, copy);
  dst[copy] = '\0';
  *cursor = p + n;
  return true;
}

} // namespace

// Applies a decoded (or raw) navigation payload. Defined below
// processNavigationBinary so both wire formats share one parser.
static bool applyNavigationPayload(const uint8_t* payload, size_t payloadLen);

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

  // 4. Phone clock sync — Unix epoch seconds (UTC) + timezone offset
  if (doc["ts"].is<uint64_t>() || doc["ts"].is<long>()) {
    const uint64_t ts = doc["ts"].as<uint64_t>();
    if (ts > 1000000000ULL) {  // sanity: must be after year 2001
      gClockEpochSec = ts;
      gClockSyncMs   = millis();
      gClockSynced   = true;
      recognized = true;
    }
  }
  if (doc["tz_offset"].is<int>()) {
    gClockTzOffsetSec = doc["tz_offset"].as<int>();
  }

  return recognized;
}

bool processNavigationBinary(const uint8_t* frame, size_t len) {
  if (frame == nullptr || len < kNavBinHeaderSize || frame[0] != kNavBinMagic0 ||
      frame[1] != kNavBinMagic1) return false;
  const size_t payloadLen = readU16Le(frame + 3);
  if (payloadLen > kNavBinMaxPayload || len != kNavBinHeaderSize + payloadLen) return false;

  const uint8_t* payload = frame + kNavBinHeaderSize;
  if (frame[2] == kNavBinTypeNav) {
    return applyNavigationPayload(payload, payloadLen);
  }
  if (frame[2] != kNavBinTypeHuffman || payloadLen < 2) return false;

  // Huffman frame: [uncompressed len LE16][bitstream]
  const size_t contentLen = readU16Le(payload);
  if (contentLen > kNavBinMaxContent || contentLen < 12) return false;
  static uint8_t sDecoded[kNavBinMaxContent];
  if (!decodeHuffman(payload + 2, payloadLen - 2, sDecoded, contentLen)) return false;
  return applyNavigationPayload(sDecoded, contentLen);
}

static bool applyNavigationPayload(const uint8_t* payload, size_t payloadLen) {
  if (payload == nullptr || payloadLen < 12) return false;

  const uint8_t* p = payload;
  const uint8_t* end = payload + payloadLen;
  const uint8_t flags = *p++;
  const int16_t speed = static_cast<int16_t>(readU16Le(p)); p += 2;
  const uint8_t speedLimit = *p++;
  const uint32_t epoch = readU32Le(p); p += 4;
  const int32_t tzOffset = static_cast<int32_t>(readU32Le(p)); p += 4;

  char road[sizeof(gNavData.nextRoad)];
  char sub[sizeof(gNavData.nextRoadSub)];
  char distance[sizeof(gNavData.distance)];
  char eta[sizeof(gNavData.eta)];
  char ete[sizeof(gNavData.ete)];
  char totalDistance[sizeof(gNavData.totalDistance)];
  if (!readField(road, sizeof(road), &p, end) || !readField(sub, sizeof(sub), &p, end) ||
      !readField(distance, sizeof(distance), &p, end) || !readField(eta, sizeof(eta), &p, end) ||
      !readField(ete, sizeof(ete), &p, end) || !readField(totalDistance, sizeof(totalDistance), &p, end)) return false;

  const bool hasIcon = (flags & 0x02) != 0;
  if (hasIcon) {
    if (static_cast<size_t>(end - p) != kIconBufferSize) return false;
  } else if (p != end) return false;

  const bool wasDifferentRoad = road[0] != '\0' && strcmp(gNavData.nextRoad, road) != 0;
  gNavData.newTurnManeuver = gNavData.newTurnManeuver || wasDifferentRoad;
  strcpy(gNavData.nextRoad, road);
  strcpy(gNavData.nextRoadSub, sub);
  strcpy(gNavData.distance, distance);
  strcpy(gNavData.eta, eta);
  strcpy(gNavData.ete, ete);
  strcpy(gNavData.totalDistance, totalDistance);
  gNavData.speed = speed;
  gNavData.speedLimit = speedLimit;
  gNavData.displayBacklight = (flags & 0x04) != 0;
  gNavData.active = (flags & 0x01) != 0;
  gNavData.hasIcon = hasIcon;
  if (hasIcon) {
    memcpy(gNavData.iconBitmap, p, kIconBufferSize);
    // Row-differenced icons: each row XOR the row above, so undo top-down.
    if ((flags & kNavBinFlagIconRowDiff) != 0) {
      for (size_t row = 1; row < kIconHeight; ++row) {
        for (size_t col = 0; col < kIconWidth / 8; ++col) {
          const size_t i = row * (kIconWidth / 8) + col;
          gNavData.iconBitmap[i] ^= gNavData.iconBitmap[i - (kIconWidth / 8)];
        }
      }
    }
  } else {
    memset(gNavData.iconBitmap, 0, sizeof(gNavData.iconBitmap));
  }
  gNavData.lastUpdateMs = millis();

  if (epoch > 1000000000UL) {
    gClockEpochSec = epoch;
    gClockSyncMs = millis();
    gClockSynced = true;
  }
  gClockTzOffsetSec = tzOffset;
  return true;
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
  const ClockFields f = epochToFields(currentLocalEpochSec());
  snprintf(buf, bufLen, "%02u:%02u", f.h, f.m);
}

void getClockDateStr(char* buf, size_t bufLen) {
  if (!gClockSynced) {
    strncpy(buf, "--/--", bufLen);
    buf[bufLen - 1] = '\0';
    return;
  }
  const ClockFields f = epochToFields(currentLocalEpochSec());
  snprintf(buf, bufLen, "%02u/%02u", f.d, f.mo);
}
