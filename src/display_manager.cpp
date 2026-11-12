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
    const uint8_t distanceSize = strlen(nav.distance) <= 5 ? 2 : 1;
    gDisplay.setTextSize(distanceSize);
    gDisplay.setCursor(kTextX, distanceSize == 2 ? 5 : 10);
    gDisplay.print(nav.distance);
  }

  // Speed badge is right aligned and gets reserved space so it cannot overlap distance.
  gDisplay.setTextSize(1);
  if (nav.speed >= 0) {
    char speedText[8];
    snprintf(speedText, sizeof(speedText), "%d", nav.speed);
    const int16_t speedWidth = static_cast<int16_t>(strlen(speedText) * 6 + 8);
    const int16_t speedX = kSpeedRight - speedWidth;
    gDisplay.drawRoundRect(speedX, 2, speedWidth, 26, 3, SSD1306_WHITE);
    gDisplay.setCursor(speedX + 4, 5);
    gDisplay.print(speedText);
    gDisplay.setCursor(speedX + 4, 17);
    gDisplay.print("km/h");
    if (nav.speedLimit > 0 && nav.speed > nav.speedLimit) {
      gDisplay.drawRect(speedX - 1, 1, speedWidth + 2, 28, SSD1306_WHITE);
    }
  }

  // Split the maneuver area from the road/trip details.
  gDisplay.drawFastHLine(0, 33, OLED_WIDTH, SSD1306_WHITE);

  // Road name: fit the single line to the screen rather than drawing off-screen.
  gDisplay.setTextSize(1);
  const char* road = nav.nextRoad[0] != '\0' ? nav.nextRoad : "Proceed to route";
  constexpr size_t kRoadLineChars = (OLED_WIDTH - 4) / 6;
  if (strlen(road) > kRoadLineChars) {
    char roadLine[kRoadLineChars + 1];
    snprintf(roadLine, sizeof(roadLine), "%.*s...", static_cast<int>(kRoadLineChars - 3), road);
    gDisplay.setCursor(2, 38);
    gDisplay.print(roadLine);
  } else {
    gDisplay.setCursor(2, 38);
    gDisplay.print(road);
  }

  // A restrained second line: maneuver cue, or concise ETA/ETE/remaining distance.
  gDisplay.setCursor(2, 52);
  if (nav.nextRoadSub[0] != '\0') {
    gDisplay.print(nav.nextRoadSub);
  } else if (nav.ete[0] != '\0' && nav.totalDistance[0] != '\0') {
    gDisplay.print(nav.ete);
    gDisplay.print(" ");
    gDisplay.print(nav.totalDistance);
  } else if (nav.eta[0] != '\0') {
    gDisplay.print("ETA ");
    gDisplay.print(nav.eta);
  } else if (nav.ete[0] != '\0') {
    gDisplay.print(nav.ete);
    gDisplay.print(" left");
  }

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
