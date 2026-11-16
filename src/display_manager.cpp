#include "display_manager.h"
#include "ui/display_internal.h"
#include "ui/settings.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {

Adafruit_SSD1306 gDisplay(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool gDisplayReady = false;
uint8_t gDisplayAddress = 0;

// Full framebuffer push is the most expensive thing the loop does. Keep the
// last frame sent so unchanged pages cost no I2C traffic at all, and push only
// the dirty column span of the pages that actually changed.
constexpr uint8_t kPages = (OLED_HEIGHT + 7) / 8;
uint8_t gSentFrame[OLED_WIDTH * kPages];
bool gSentFrameValid = false;

// Keep control byte + payload within the common 32-byte Wire buffer.
constexpr uint8_t kI2cChunk = 31;

// Write one 8-row page band over I2C, limited to [colStart, colEnd].
bool sendPage(uint8_t page, uint8_t colStart, uint8_t colEnd, const uint8_t* row) {
  const uint8_t setup[] = {
      0x00, SSD1306_PAGEADDR, page, page, SSD1306_COLUMNADDR, colStart, colEnd};
  Wire.beginTransmission(gDisplayAddress);
  const size_t setupWritten = Wire.write(setup, sizeof(setup));
  const uint8_t setupError = Wire.endTransmission();
  if (setupWritten != sizeof(setup) || setupError != 0) return false;

  const uint8_t count = colEnd - colStart + 1;
  for (uint8_t sent = 0; sent < count;) {
    const uint8_t n = count - sent < kI2cChunk ? count - sent : kI2cChunk;
    Wire.beginTransmission(gDisplayAddress);
    const size_t controlWritten = Wire.write(0x40);
    const size_t dataWritten = Wire.write(row + colStart + sent, n);
    const uint8_t dataError = Wire.endTransmission();
    if (controlWritten != 1 || dataWritten != n || dataError != 0) return false;
    sent += n;
  }
  return true;
}

void presentFrame() {
  const uint8_t* buffer = gDisplay.getBuffer();
  if (buffer == nullptr) return;

  if (gSentFrameValid) {
    bool anySent = false;
    for (uint8_t page = 0; page < kPages; ++page) {
      const uint8_t* row  = buffer + page * OLED_WIDTH;
      uint8_t* sentRow    = gSentFrame + page * OLED_WIDTH;
      if (memcmp(row, sentRow, OLED_WIDTH) == 0) continue;
      // Narrow the transfer to the columns that differ in this page.
      uint8_t first = 0;
      uint8_t last = OLED_WIDTH - 1;
      while (first < OLED_WIDTH && row[first] == sentRow[first]) ++first;
      while (last > first && row[last] == sentRow[last]) --last;
      if (!sendPage(page, first, last, row)) {
        // Panel state is now uncertain; resync with a full frame next time.
        gSentFrameValid = false;
        return;
      }
      memcpy(sentRow, row, OLED_WIDTH);
      anySent = true;
    }
    if (anySent) return;
  } else {
    // Panel contents unknown after reset/hotplug: send the whole frame once.
    gDisplay.display();
    memcpy(gSentFrame, buffer, sizeof(gSentFrame));
    gSentFrameValid = true;
  }
}

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
constexpr int16_t kScrollPxPerTick = 2;               // pixels scrolled each tick
constexpr uint32_t kScrollStepMs   = 100;             // one scroll tick every 100 ms
constexpr uint32_t kScrollPauseMs  = 1500;            // hold at start before scrolling

static char  gScrollRoad[64] = "";
static int16_t gScrollOffsetPx = 0;                   // how many px we have scrolled left so far
static uint32_t gScrollStartMs = 0;                   // millis() when this road string first appeared

int16_t roadTextWidth(const char* road) {
  return static_cast<int16_t>(strlen(road)) * kCharW;
}

// Offset is derived from elapsed time, not frame count, so a slow I2C frame
// cannot change the scroll speed. Returns 0 when paused or just wrapped.
int16_t marqueeOffset(const char* road, int16_t textW) {
  if (textW <= kRoadW || strcmp(gScrollRoad, road) != 0) return 0;
  const uint32_t elapsed = millis() - gScrollStartMs;
  const uint32_t ticks = elapsed > kScrollPauseMs
                             ? (elapsed - kScrollPauseMs) / kScrollStepMs
                             : 0;
  const uint32_t maxScroll = static_cast<uint32_t>(textW + kCharW * 2);
  const uint32_t travel = ticks * kScrollPxPerTick;
  return travel >= maxScroll ? 0 : static_cast<int16_t>(travel);
}

// Refresh scroll state for this road (resets on a new label, restarts the pause
// after a wrap) and store the offset the renderer should draw at.
void updateRoadScroll(const char* road) {
  const int16_t textW = roadTextWidth(road);

  if (textW <= kRoadW) {
    // Short enough — just draw it, no scrolling needed.
    if (strcmp(gScrollRoad, road) != 0) {
      strncpy(gScrollRoad, road, sizeof(gScrollRoad) - 1);
      gScrollRoad[sizeof(gScrollRoad) - 1] = '\0';
    }
    gScrollOffsetPx = 0;
    return;
  }

  if (strcmp(gScrollRoad, road) != 0) {
    // New road arrived — reset scroll state.
    strncpy(gScrollRoad, road, sizeof(gScrollRoad) - 1);
    gScrollRoad[sizeof(gScrollRoad) - 1] = '\0';
    gScrollOffsetPx = 0;
    gScrollStartMs  = millis();
  }

  const int16_t offset = marqueeOffset(road, textW);
  // Wrapped past the end: restart with the usual pause at the beginning.
  if (offset < gScrollOffsetPx) gScrollStartMs = millis();
  gScrollOffsetPx = offset;
}

// Render one road label line into the OLED buffer at the prepared offset.
// If the text fits, draw it normally.
// If it is too long, scroll it left one frame at a time, then loop.
void renderRoadMarquee(const char* road) {
  const int16_t offset = gScrollOffsetPx;
  // Draw characters directly without heap allocation (no GFXcanvas1 malloc)
  const int16_t roadLen = static_cast<int16_t>(strlen(road));
  const int16_t startChar = offset > 0 ? (offset / kCharW) : 0;
  for (int16_t i = startChar; i < roadLen; ++i) {
    const int16_t charX = kRoadX + (i * kCharW) - offset;
    if (charX >= OLED_WIDTH) break;
    gDisplay.drawChar(charX, kRoadY, road[i], SSD1306_WHITE, SSD1306_BLACK, 1);
  }
  // Keep the background empty; characters are clipped naturally by the framebuffer.
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
  if (gDisplayReady) {
    // 100 kHz makes one 1024-byte frame block the loop for ~90 ms. The SSD1306
    // handles 400 kHz, which keeps a scrolling frame inside one loop turn.
    // Only raised after the panel answered at the safe default rate.
    Wire.setClock(400000);
  }
  // The panel was just reset; whatever was last sent is no longer on screen.
  gSentFrameValid = false;
  return gDisplayReady;
}

void renderBootAnimation(uint32_t elapsedMs) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(28, 8);
  gDisplay.print("ESP32 Studio");
  gDisplay.drawRoundRect(14, 27, 100, 14, 4, SSD1306_WHITE);
  const uint8_t progress = elapsedMs >= 1200 ? 96 : elapsedMs * 96 / 1200;
  gDisplay.fillRoundRect(16, 29, progress, 10, 3, SSD1306_WHITE);
  gDisplay.setCursor(36, 51);
  gDisplay.print("Starting...");
  presentFrame();
}

bool isDisplayReady() {
  static uint32_t lastProbeMs = 0;
  constexpr uint32_t kHotplugProbeIntervalMs = 1000;
  const uint32_t now = millis();
  if (now - lastProbeMs < kHotplugProbeIntervalMs) return gDisplayReady;
  lastProbeMs = now;

  if (gDisplayReady) {
    Wire.beginTransmission(gDisplayAddress);
    if (Wire.endTransmission() != 0) {
      gDisplayReady = false;
      gDisplayAddress = 0;
      // A failed probe may be a marginal 400 kHz link; retry at the default rate.
      Wire.setClock(100000);
    }
  }

  if (!gDisplayReady) {
    gDisplayReady = initDisplay(OLED_SDA_PIN, OLED_SCL_PIN);
  }
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

void renderNavHud(const NavigationData& nav, bool speedFocused) {
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
  if (!speedFocused) {
    if (nav.distance[0] != '\0') {
      gDisplay.setTextSize(2);
      gDisplay.setCursor(kTextX, 5);
      gDisplay.print(nav.distance);
    }

    gDisplay.setTextSize(1);
    if (isClockSynced()) {
      char timeStr[6], dateStr[6];
      getClockTimeStr(timeStr, sizeof(timeStr));
      getClockDateStr(dateStr, sizeof(dateStr));
      char clockLine[12];
      snprintf(clockLine, sizeof(clockLine), "%s %s", dateStr, timeStr);
      gDisplay.setCursor(kTextX, 23);
      gDisplay.print(clockLine);
    }

    // Speed badge is right aligned and gets reserved space so it cannot overlap distance.
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
  } else {
    // Speed-focused mode: large km/h, small distance and clock.
    if (nav.distance[0] != '\0') {
      gDisplay.setCursor(kTextX, 25);
      gDisplay.printf("%.8s", nav.distance);
    }
    if (isClockSynced()) {
      char timeStr[6];
      getClockTimeStr(timeStr, sizeof(timeStr));
      gDisplay.setCursor(96, 25);
      gDisplay.print(timeStr);
    }
    if (nav.speed >= 0) {
      char speedText[8];
      snprintf(speedText, sizeof(speedText), "%d", nav.speed);
      gDisplay.setTextSize(2);
      const int16_t speedWidth = static_cast<int16_t>(strlen(speedText) * 12);
      gDisplay.setCursor(kTextX, 4);
      gDisplay.print(speedText);
      gDisplay.setTextSize(1);
      gDisplay.setCursor(kTextX + speedWidth + 4, 11);
      gDisplay.print("kmh");
      if (nav.speedLimit > 0 && nav.speed > nav.speedLimit) {
        gDisplay.drawRect(35, 1, OLED_WIDTH - 36, 31, SSD1306_WHITE);
      }
    }
  }

  // Split the maneuver area from the road/trip details.
  gDisplay.drawFastHLine(0, 33, OLED_WIDTH, SSD1306_WHITE);

  // Road name: static if it fits, animated marquee if it overflows.
  gDisplay.setTextSize(1);
  gDisplay.setTextWrap(false);
  const char* road = nav.nextRoad[0] != '\0' ? nav.nextRoad : "Proceed to route";
  updateRoadScroll(road);
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
    pos += snprintf(tripLine + pos, sizeof(tripLine) - pos, pos > 0 ? "-%s" : "%s", nav.totalDistance);
  }
  if (nav.eta[0] != '\0' && pos < sizeof(tripLine) - 1) {
    pos += snprintf(tripLine + pos, sizeof(tripLine) - pos, pos > 0 ? "-%s" : "%s", nav.eta);
  }

  if (tripLine[0] != '\0') {
    // If nextRoadSub already occupies line 1, trip goes on line 2; otherwise use line 1.
    const int16_t tripY = nav.nextRoadSub[0] != '\0' ? 57 : 47;
    gDisplay.setCursor(2, tripY);
    gDisplay.print(tripLine);
  }
  gDisplay.setTextWrap(true);

  presentFrame();

  // If a new turn was detected or backlight preference set, update effects
  if (nav.newTurnManeuver) {
    flashBacklight();
    getNavigationData().newTurnManeuver = false;
  }
  handleFlashEffect();
}

void renderGameFrame(bool flappy, int16_t playerY, int16_t obstacleX, uint32_t score, bool gameOver) {
  if (!gDisplayReady) return;
  prepareForI2C();

  constexpr int16_t kGroundY = 53;
  constexpr int16_t kObstacleW = 7;
  constexpr int16_t kObstacleH = 12;

  gDisplay.clearDisplay();
  gDisplay.setTextSize(1);
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(2, 2);
  gDisplay.printf("%s %lu", flappy ? "Flap" : "Dino", static_cast<unsigned long>(score));

  gDisplay.drawFastHLine(0, kGroundY, OLED_WIDTH, SSD1306_WHITE);

  // Obstacle: a wall for Dino, a pipe gap for Flappy.
  if (flappy) {
    const int16_t gapTop = kObstacleH + 14;
    const int16_t lowerHeight = kGroundY - gapTop - 20;
    gDisplay.fillRect(obstacleX, 0, kObstacleW, gapTop, SSD1306_WHITE);
    if (lowerHeight > 0) {
      gDisplay.fillRect(obstacleX, gapTop + 20, kObstacleW, lowerHeight, SSD1306_WHITE);
    }
  } else {
    gDisplay.fillRect(obstacleX, kGroundY - kObstacleH, kObstacleW, kObstacleH, SSD1306_WHITE);
  }

  // Player: 8x8 block in both games; position is owned by the game logic.
  gDisplay.fillRect(18, playerY, 8, 8, SSD1306_WHITE);

  if (gameOver) {
    gDisplay.fillRect(20, 20, 88, 18, SSD1306_BLACK);
    gDisplay.drawRect(20, 20, 88, 18, SSD1306_WHITE);
    gDisplay.setCursor(30, 25);
    gDisplay.print("Game over");
  }
  presentFrame();
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

  presentFrame();
  handleFlashEffect();
}

namespace ui::display {

void begin() {
  if (!gDisplayReady) return;
  gDisplay.clearDisplay();
  presentFrame();
}

void settings(const ui::settings::Values& values, uint8_t selectedOption, bool editing) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setTextWrap(false);
  gDisplay.setCursor(2, 2);
  gDisplay.print("Settings");
  gDisplay.drawFastHLine(0, 11, OLED_WIDTH, SSD1306_WHITE);

  uint8_t count = 0;
  const ui::settings::model::Entry* items = ui::settings::model::entries(count);
  constexpr uint8_t kVisibleItems = 4;
  const uint8_t first = selectedOption >= kVisibleItems ? selectedOption - kVisibleItems + 1 : 0;
  for (uint8_t row = 0; row < kVisibleItems && first + row < count; ++row) {
    const uint8_t index = first + row;
    const int16_t y = 14 + row * 10;
    if (index == selectedOption) {
      gDisplay.fillRoundRect(0, y - 1, OLED_WIDTH, 10, 2, SSD1306_WHITE);
      gDisplay.setTextColor(SSD1306_BLACK);
    } else {
      gDisplay.setTextColor(SSD1306_WHITE);
    }
    gDisplay.setCursor(3, y);
    gDisplay.print(items[index].label);
  }

  // Value line: the current selection's value, in angle brackets while editing.
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.drawFastHLine(0, 53, OLED_WIDTH, SSD1306_WHITE);
  char buffer[40];
  const char* shown = ui::settings::valueText(values, selectedOption, buffer, sizeof(buffer));
  gDisplay.setCursor(2, 56);
  if (editing) gDisplay.print('<');
  gDisplay.printf("%.18s", shown);
  if (editing) gDisplay.print('>');
  presentFrame();
}

void keyboard(const char* label, const char* text, bool secret, uint8_t page, uint8_t cell) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setTextWrap(false);

  // Header: field name, then the page tag right-aligned.
  gDisplay.setCursor(2, 2);
  gDisplay.printf("%.15s", label != nullptr ? label : "");
  using namespace ui::settings::model::keyboard;
  gDisplay.setCursor(OLED_WIDTH - 2 - 6 * static_cast<int16_t>(strlen(pageLabel(page))), 2);
  gDisplay.print(pageLabel(page));
  gDisplay.drawFastHLine(0, 11, OLED_WIDTH, SSD1306_WHITE);

  constexpr int16_t kGridX = 4;
  constexpr int16_t kGridY = 13;
  constexpr int16_t kCellW = 12;
  constexpr int16_t kCellH = 9;
  for (uint8_t index = 0; index < kCells; ++index) {
    const Cell key = cellAt(page, index);
    if (key.action == Action::Empty) continue;
    int16_t x;
    int16_t y;
    int16_t w = kCellW;
    if (index < kCharCells) {
      x = kGridX + (index % kCols) * kCellW;
      y = kGridY + (index / kCols) * kCellH;
    } else {
      // SP DEL PG OK sit in a widened row below the grid.
      constexpr int16_t kKeyW = 30;
      w = kKeyW;
      x = static_cast<int16_t>(2 + (index - kCharCells) * (kKeyW + 1));
      y = 42;
    }
    const bool focused = index == cell;
    if (focused) gDisplay.fillRoundRect(x, y, w, kCellH, 2, SSD1306_WHITE);
    gDisplay.setTextColor(focused ? SSD1306_BLACK : SSD1306_WHITE);
    if (index < kCharCells) {
      gDisplay.setCursor(x + 3, y + 1);
      gDisplay.print(key.ch);
    } else {
      static constexpr const char* kControlLabels[] = {"SP", "DEL", "PG", "OK"};
      const char* controlLabel = kControlLabels[index - kCharCells];
      gDisplay.setCursor(x + (w - 6 * static_cast<int16_t>(strlen(controlLabel))) / 2, y + 1);
      gDisplay.print(controlLabel);
    }
  }

  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.drawFastHLine(0, 53, OLED_WIDTH, SSD1306_WHITE);
  // Footer: the tail of the value, masked when it is a secret.
  constexpr uint8_t kFooterChars = 20;
  const size_t length = text != nullptr ? strlen(text) : 0;
  const size_t start = length > kFooterChars ? length - kFooterChars : 0;
  gDisplay.setCursor(2, 56);
  for (size_t i = start; i < length; ++i) gDisplay.print(secret ? '*' : text[i]);
  if (length == 0) {
    gDisplay.setCursor(2, 56);
    gDisplay.print(secret ? "(type a password)" : "(empty)");
  }
  presentFrame();
}

void buttonMonitor(uint16_t analogValue, uint8_t activePin, const char* label) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(2, 3);
  gDisplay.print("Button monitor");
  gDisplay.drawFastHLine(0, 14, OLED_WIDTH, SSD1306_WHITE);
  gDisplay.setCursor(2, 23);
  gDisplay.printf("GPIO %u  ADC %u", activePin, analogValue);
  gDisplay.setTextSize(2);
  gDisplay.setCursor(2, 39);
  gDisplay.print(label);
  presentFrame();
}

void text(const char* title, const char* body) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(2, 3);
  gDisplay.print(title);
  gDisplay.drawFastHLine(0, 14, OLED_WIDTH, SSD1306_WHITE);
  gDisplay.setCursor(2, 23);
  gDisplay.print(body);
  presentFrame();
}

void text(const char* title, const char* body, const BluetoothDeviceInfo* devices, uint8_t count,
          uint8_t selectedDevice) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(2, 3);
  gDisplay.print(title);
  gDisplay.drawFastHLine(0, 14, OLED_WIDTH, SSD1306_WHITE);
  if (count == 0) {
    gDisplay.setCursor(2, 23);
    gDisplay.print("Scanning...");
    gDisplay.setCursor(2, 36);
    gDisplay.print(body);
  } else {
    const uint8_t first = selectedDevice > 2 ? selectedDevice - 2 : 0;
    for (uint8_t row = 0; row < 5 && first + row < count; ++row) {
      const uint8_t index = first + row;
      const int16_t y = 18 + row * 9;
      if (index == selectedDevice) {
        gDisplay.fillRoundRect(1, y - 1, OLED_WIDTH - 2, 9, 2, SSD1306_WHITE);
        gDisplay.setTextColor(SSD1306_BLACK);
      } else gDisplay.setTextColor(SSD1306_WHITE);
      gDisplay.setCursor(4, y);
      gDisplay.printf("%.14s %d dBm", devices[index].name, devices[index].rssi);
    }
    gDisplay.setTextColor(SSD1306_WHITE);
    gDisplay.setCursor(2, 59);
    gDisplay.print("Btn1/3 Browse  Btn2 Back");
  }
  presentFrame();
}

#if defined(USE_WIFI)
void wifiScan(uint8_t selected, const WifiNetwork* networks, uint8_t count, bool scanning) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(2, 3);
  gDisplay.print(scanning ? "Wi-Fi scan..." : "Wi-Fi networks");
  gDisplay.drawFastHLine(0, 14, OLED_WIDTH, SSD1306_WHITE);
  if (count == 0) {
    gDisplay.setCursor(2, 26);
    gDisplay.print(scanning ? "Searching..." : "No networks");
  } else {
    const uint8_t first = selected > 3 ? selected - 3 : 0;
    for (uint8_t row = 0; row < 4 && first + row < count; ++row) {
      const uint8_t index = first + row;
      const int16_t y = 18 + row * 9;
      if (index == selected) {
        gDisplay.fillRoundRect(1, y - 1, OLED_WIDTH - 2, 9, 2, SSD1306_WHITE);
        gDisplay.setTextColor(SSD1306_BLACK);
      } else gDisplay.setTextColor(SSD1306_WHITE);
      gDisplay.setCursor(4, y);
      gDisplay.printf("%.14s %d", networks[index].ssid, networks[index].rssi);
    }
  }
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(2, 57);
  gDisplay.print("1/3 Scroll  2 Select");
  presentFrame();
}
#endif

void x3Remote(const char* status) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(2, 3);
  gDisplay.print("Insta360 remote");
  gDisplay.drawFastHLine(0, 14, OLED_WIDTH, SSD1306_WHITE);
  gDisplay.setCursor(2, 24);
  gDisplay.print(status);
  gDisplay.setCursor(2, 40);
  gDisplay.print("Btn1 Mode");
  gDisplay.setCursor(2, 50);
  gDisplay.print("Btn2 Shutter");
  gDisplay.setCursor(2, 58);
  gDisplay.print("Hold Btn2 Back");
  presentFrame();
}

void menu(uint8_t selectedItem, const char* const* items, uint8_t itemCount) {
  if (!gDisplayReady) return;
  prepareForI2C();
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setTextWrap(false);
  gDisplay.setCursor(2, 2);
  gDisplay.print("Main menu");
  gDisplay.drawFastHLine(0, 12, OLED_WIDTH, SSD1306_WHITE);

  constexpr uint8_t kVisibleItems = 4;
  const uint8_t first = selectedItem >= kVisibleItems
                            ? selectedItem - kVisibleItems + 1
                            : 0;
  for (uint8_t row = 0; row < kVisibleItems && first + row < itemCount; ++row) {
    const uint8_t index = first + row;
    const int16_t y = 16 + row * 10;
    if (index == selectedItem) {
      gDisplay.fillRoundRect(1, y - 1, OLED_WIDTH - 2, 10, 2, SSD1306_WHITE);
      gDisplay.setTextColor(SSD1306_BLACK);
    } else {
      gDisplay.setTextColor(SSD1306_WHITE);
    }
    gDisplay.setCursor(5, y);
    gDisplay.print(items[index]);
  }
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(2, 57);
  gDisplay.print("1/3 Scroll  2 Select");
  presentFrame();
}

}  // namespace ui::display

void updateDisplay(const char* statusText, const char* btMessage, uint32_t cycle) {
  if (!gDisplayReady) return;

  const NavigationData& nav = getNavigationData();
  if (isNavigationActive()) {
    renderNavHud(nav);
  } else {
    renderStatusScreen(statusText, btMessage, nav.speed, cycle);
  }
}
