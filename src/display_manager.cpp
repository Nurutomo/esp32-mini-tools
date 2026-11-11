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

  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);

  // 1. Maneuver Icon (32x32 at top-left 0, 0)
  if (nav.hasIcon) {
    gDisplay.drawBitmap(0, 0, nav.iconBitmap, 32, 32, SSD1306_WHITE);
  } else {
    // Elegant fallback turn marker
    gDisplay.drawRoundRect(2, 2, 28, 28, 4, SSD1306_WHITE);
    gDisplay.drawLine(16, 6, 16, 26, SSD1306_WHITE);
    gDisplay.drawLine(16, 6, 10, 13, SSD1306_WHITE);
    gDisplay.drawLine(16, 6, 22, 13, SSD1306_WHITE);
  }

  // Vertical separator next to maneuver icon
  gDisplay.drawLine(33, 0, 33, 31, SSD1306_WHITE);

  // 2. Distance to turn (Bold TextSize 2 if short, Size 1 if long)
  if (strlen(nav.distance) > 0) {
    if (strlen(nav.distance) <= 6) {
      gDisplay.setTextSize(2);
      gDisplay.setCursor(37, 1);
      gDisplay.print(nav.distance);
    } else {
      gDisplay.setTextSize(1);
      gDisplay.setCursor(37, 4);
      gDisplay.print(nav.distance);
    }
  }

  // 3. Trip info (ETA / ETE / Remaining Distance)
  gDisplay.setTextSize(1);
  gDisplay.setCursor(37, 20);
  if (strlen(nav.ete) > 0 && strlen(nav.totalDistance) > 0) {
    gDisplay.printf("%s (%s)", nav.ete, nav.totalDistance);
  } else if (strlen(nav.eta) > 0) {
    gDisplay.printf("ETA %s", nav.eta);
  } else if (strlen(nav.ete) > 0) {
    gDisplay.printf("%s rem", nav.ete);
  }

  // 4. Real-time GPS Speedometer in top right corner
  if (nav.speed >= 0) {
    gDisplay.setTextSize(1);
    gDisplay.setCursor(102, 1);
    gDisplay.printf("%3d", nav.speed);
    gDisplay.setCursor(102, 11);
    gDisplay.print("km/h");

    // Overspeed warning indicator
    if (nav.speedLimit > 0 && nav.speed > nav.speedLimit) {
      gDisplay.drawRect(100, 0, 27, 21, SSD1306_WHITE);
    }
  }

  // Horizontal separator between top HUD and bottom street info
  gDisplay.drawLine(0, 32, 127, 32, SSD1306_WHITE);

  // 5. Main street / highway name
  gDisplay.setTextSize(1);
  gDisplay.setCursor(0, 36);
  if (strlen(nav.nextRoad) > 0) {
    gDisplay.print(nav.nextRoad);
  } else {
    gDisplay.print("Proceed to route");
  }

  // 6. Sub-road / maneuver instruction
  gDisplay.setCursor(0, 50);
  if (strlen(nav.nextRoadSub) > 0) {
    gDisplay.print(nav.nextRoadSub);
  } else if (strlen(nav.eta) > 0 && strlen(nav.totalDistance) > 0) {
    gDisplay.printf("ETA: %s | %s", nav.eta, nav.totalDistance);
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
