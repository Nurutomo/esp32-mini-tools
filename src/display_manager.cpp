#include "display_manager.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {

Adafruit_SSD1306 gDisplay(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool gDisplayReady = false;
uint8_t gDisplayAddress = 0;

constexpr uint8_t LED_ON  = LED_ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t LED_OFF = LED_ACTIVE_LOW ? HIGH : LOW;

// Flash / Invert state
int8_t gFlashCount = -1;
uint32_t gLastFlashMs = 0;
bool gCurrentInvert = false;

// Road-name marquee state
// GFX text size 1 = 6×8 px per char (5 px glyph + 1 px gap)
constexpr int16_t kCharW = 6;
constexpr int16_t kRoadY = 38;
constexpr int16_t kRoadX = 2;
constexpr int16_t kRoadW = OLED_WIDTH - kRoadX;      // usable pixels (126)
constexpr int16_t kScrollPxPerTick = 2;               // pixels scrolled each 500 ms tick
constexpr uint32_t kScrollPauseMs  = 1500;            // hold at start before scrolling

static char  gScrollRoad[64] = "";
static int16_t gScrollOffsetPx = 0;                   // how many px we have scrolled left so far
static uint32_t gScrollStartMs = 0;                   // millis() when this road string first appeared
static bool  gScrollPaused = true;

// Render one road label line into the OLED buffer.
// If the text fits, draw it normally.
// If it is too long, scroll it left one frame at a time, then loop.
void renderRoadMarquee(const char* road) {
  const int16_t textW = static_cast<int16_t>(strlen(road)) * kCharW;

  if (textW <= kRoadW) {
    // Short enough — just draw it, no scrolling needed.
    if (strcmp(gScrollRoad, road) != 0) {
      strncpy(gScrollRoad, road, sizeof(gScrollRoad) - 1);
      gScrollRoad[sizeof(gScrollRoad) - 1] = '\0';
      gScrollOffsetPx = 0;
      gScrollStartMs  = millis();
      gScrollPaused   = false;
    }
    gDisplay.setTextSize(1);
    gDisplay.setTextWrap(false);
    gDisplay.setCursor(kRoadX, kRoadY);
    gDisplay.print(road);
    return;
  }

  // ── Long road: marquee ───────────────────────────────────────────────────
  if (strcmp(gScrollRoad, road) != 0) {
    // New road arrived — reset scroll state.
    strncpy(gScrollRoad, road, sizeof(gScrollRoad) - 1);
    gScrollRoad[sizeof(gScrollRoad) - 1] = '\0';
    gScrollOffsetPx = 0;
    gScrollStartMs  = millis();
    gScrollPaused   = true;
  }

  // Advance scroll offset after the initial pause.
  const uint32_t elapsed = millis() - gScrollStartMs;
  if (gScrollPaused && elapsed >= kScrollPauseMs) {
    gScrollPaused = false;
  }
  if (!gScrollPaused) {
    gScrollOffsetPx += kScrollPxPerTick;
    // Once the text has fully scrolled off the left edge, loop back.
    const int16_t maxScroll = textW + kCharW * 2;  // a small gap before looping
    if (gScrollOffsetPx >= maxScroll) {
      gScrollOffsetPx = 0;
      gScrollStartMs  = millis();
      gScrollPaused   = true;   // pause again at the beginning of the loop
    }
  }

  // Draw using a 1-bit canvas so we can clip the region correctly.
  // Canvas width = full text width; blit only the visible slice into the frame buffer.
  const int16_t canvasW = textW + kCharW * 2;
  GFXcanvas1 canvas(canvasW, 8);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);
  canvas.setTextColor(1);
  canvas.setCursor(0, 0);
  canvas.print(road);

  // Blit the visible slice [gScrollOffsetPx .. gScrollOffsetPx + kRoadW - 1] onto the display.
  for (int16_t dx = 0; dx < kRoadW; dx++) {
    const int16_t srcX = gScrollOffsetPx + dx;
    if (srcX < 0 || srcX >= canvasW) continue;
    for (int16_t dy = 0; dy < 8; dy++) {
      if (canvas.getPixel(srcX, dy)) {
        gDisplay.drawPixel(kRoadX + dx, kRoadY + dy, SSD1306_WHITE);
      }
    }
  }
}

uint8_t scanOledAddress() {
  prepareForI2C();

  // Probe common SSD1306 addresses first (0x3C, 0x3D)
  const uint8_t commonAddresses[] = { 0x3C, 0x3D };
  for (uint8_t addr : commonAddresses) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (gDisplay.begin(SSD1306_SWITCHCAPVCC, addr)) {
        return addr;
      }
    }
  }

  // Scan range 0x08 - 0x77
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    if (addr == 0x3C || addr == 0x3D) continue;
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (gDisplay.begin(SSD1306_SWITCHCAPVCC, addr)) {
        return addr;
      }
    }
  }
  return 0;
}

} // namespace

void turnLedOff() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);
}

void turnLedOn() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON);
}

void prepareForI2C() {
  if (gDisplayReady) return;
  turnLedOff();

#if defined(CONFIG_IDF_TARGET_ESP32C3)
  Wire.end();
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
#endif
}

bool initDisplay(uint8_t sdaPin, uint8_t sclPin) {
  turnLedOff();
  Wire.begin(sdaPin, sclPin);
  gDisplayAddress = scanOledAddress();
  gDisplayReady = (gDisplayAddress != 0);
  return gDisplayReady;
}

bool isDisplayReady() {
  return gDisplayReady;
}

uint8_t getDisplayAddress() {
  return gDisplayAddress;
}

void setDisplayInverted(bool invert) {
  if (!gDisplayReady) return;
  gCurrentInvert = invert;
  gDisplay.invertDisplay(invert);
}

void flashBacklight() {
  const uint32_t now = millis();
  if (now > gLastFlashMs + 3000) {
    gLastFlashMs = now;
    gFlashCount = 0;
  }
}

static void handleFlashEffect() {
  if (gFlashCount >= 0) {
    static uint32_t lastToggleMs = 0;
    const uint32_t now = millis();
    if (now - lastToggleMs >= 100) {
      lastToggleMs = now;
      gFlashCount++;
      if (gFlashCount >= 6) { // 3 blinks
        gFlashCount = -1;
        gDisplay.invertDisplay(gCurrentInvert);
      } else {
        gDisplay.invertDisplay((gFlashCount % 2) != 0);
      }
    }
  }
}

void renderNavHud(const NavigationData& nav) {
  if (!gDisplayReady) return;
  prepareForI2C();

  // A compact 128x64 layout: icon, maneuver distance and speed in the top row;
  // road name and trip summary get the full width below the divider.
  constexpr int16_t kIconX = 2;
  constexpr int16_t kIconY = 2;
  constexpr int16_t kTextX = 36;
  constexpr int16_t kSpeedRight = 126;

  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);

  // Maneuver icon, centered in a 32x32 area. Bitmap is already converted by the phone.
  if (nav.hasIcon) {
    gDisplay.drawBitmap(kIconX, kIconY, nav.iconBitmap, 32, 32, SSD1306_WHITE);
  } else {
    gDisplay.drawRoundRect(kIconX, kIconY, 28, 28, 4, SSD1306_WHITE);
    gDisplay.drawLine(16, 25, 16, 12, SSD1306_WHITE);
    gDisplay.fillTriangle(16, 5, 9, 14, 23, 14, SSD1306_WHITE);
  }

  // Icon separator.
  gDisplay.drawFastVLine(34, 2, 28, SSD1306_WHITE);

  // Turn distance remains blank in cruise mode; never substitute route distance here.
  if (nav.distance[0] != '\0') {
    const uint8_t distanceSize = 2;
    gDisplay.setTextSize(distanceSize);
    gDisplay.setCursor(kTextX, 7);
    gDisplay.print(nav.distance);
  }

  // Speed badge is right aligned and gets reserved space so it cannot overlap distance.
  gDisplay.setTextSize(1);
  if (nav.speed >= 0) {
    char speedText[8];
    snprintf(speedText, sizeof(speedText), "%d", nav.speed);
    const int16_t speedWidth = static_cast<int16_t>(strlen(speedText) * 6 + 16);
    const int16_t speedX = kSpeedRight - speedWidth;
    gDisplay.drawRoundRect(speedX, 2, speedWidth, 26, 3, SSD1306_WHITE);
    gDisplay.setCursor(speedX + 2, 5);
    gDisplay.print(speedText);
    gDisplay.setCursor(speedX + 2, 17);
    gDisplay.print("kmh");
    if (nav.speedLimit > 0 && nav.speed > nav.speedLimit) {
      gDisplay.drawRect(speedX - 1, 1, speedWidth + 2, 28, SSD1306_WHITE);
    }
  }

  // Split the maneuver area from the road/trip details.
  gDisplay.drawFastHLine(0, 33, OLED_WIDTH, SSD1306_WHITE);

  // Road name: static if it fits, animated marquee if it overflows.
  gDisplay.setTextSize(1);
  gDisplay.setTextWrap(false);
  const char* road = nav.nextRoad[0] != '\0' ? nav.nextRoad : "Proceed to route";
  renderRoadMarquee(road);
  gDisplay.setTextWrap(true);

  // Lower section: two lines below the road name.
  // Line 1 (y=47): maneuver sub-hint (e.g. "then turn right").
  // Line 2 (y=57): compact trip summary — ETE, distance, ETA joined with " - ".
  gDisplay.setTextSize(1);
  gDisplay.setTextWrap(false);

  if (nav.nextRoadSub[0] != '\0') {
    gDisplay.setCursor(2, 47);
    gDisplay.print(nav.nextRoadSub);
  }

  // Build a compact trip summary from whatever fields are populated.
  char tripLine[32] = "";
  size_t pos = 0;
  if (nav.ete[0] != '\0') {
    pos += snprintf(tripLine + pos, sizeof(tripLine) - pos, "%s", nav.ete);
  }
  if (nav.totalDistance[0] != '\0' && pos < sizeof(tripLine) - 1) {
    pos += snprintf(tripLine + pos, sizeof(tripLine) - pos, pos > 0 ? " - %s" : "%s", nav.totalDistance);
  }
  if (nav.eta[0] != '\0' && pos < sizeof(tripLine) - 1) {
    pos += snprintf(tripLine + pos, sizeof(tripLine) - pos, pos > 0 ? " - %s" : "%s", nav.eta);
  }

  if (tripLine[0] != '\0') {
    // If nextRoadSub already occupies line 1, trip goes on line 2; otherwise use line 1.
    const int16_t tripY = nav.nextRoadSub[0] != '\0' ? 57 : 47;
    gDisplay.setCursor(2, tripY);
    gDisplay.print(tripLine);
  }
  gDisplay.setTextWrap(true);

  gDisplay.display();

  // If a new turn was detected or backlight preference set, update effects
  if (nav.newTurnManeuver) {
    flashBacklight();
    getNavigationData().newTurnManeuver = false;
  }
  handleFlashEffect();
}

void renderStatusScreen(const char* statusText, const char* btMessage, int speed, uint32_t cycle) {
  if (!gDisplayReady) return;
  prepareForI2C();

  gDisplay.clearDisplay();
  gDisplay.setTextSize(1);
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(0, 0);

  // Header
  gDisplay.println("ESP32 Studio");
  gDisplay.println("---------------------");

  // Status & BT Note
  gDisplay.printf("Status : %s\n", statusText);
  gDisplay.printf("BT Msg : %s\n", btMessage);

  // Speedometer (if GPS active) or Diagnostics
  if (speed >= 0) {
    gDisplay.printf("Speed  : %d km/h\n", speed);
  } else {
    gDisplay.printf("Cycle  : #%lu\n", cycle);
  }
  gDisplay.printf("Uptime : %lu s\n", millis() / 1000);

  gDisplay.display();
  handleFlashEffect();
}

void updateDisplay(const char* statusText, const char* btMessage, uint32_t cycle) {
  if (!gDisplayReady) return;

  const NavigationData& nav = getNavigationData();
  if (isNavigationActive()) {
    renderNavHud(nav);
  } else {
    renderStatusScreen(statusText, btMessage, nav.speed, cycle);
  }
}
