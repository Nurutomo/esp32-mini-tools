# ESP32 Mini Tools

PlatformIO firmware and a GitHub Pages web flasher for ESP32-C3 SuperMini, classic ESP32, and ESP32-S3 boards with a wired 128x64 SSD1306 I2C OLED. BLE is supported on all listed chips; Bluetooth Classic is additionally available for classic ESP32.

## Hardware

| OLED | ESP32-C3 SuperMini default |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 8 |
| SCL | GPIO 9 |

Defaults are in `include/config.h`; adjust `OLED_SDA_PIN`, `OLED_SCL_PIN`, or `OLED_I2C_ADDRESS` for your board and OLED module. Address defaults to `0x3C`. SuperMini board revisions vary, so confirm pin labels and avoid pins used by your specific board before wiring. GPIO8/9 are common defaults, not guaranteed for every revision. Use a 3.3V-compatible display.

## Build and flash locally

Install PlatformIO Core with Python 3.10 or newer, then run:

```sh
pio run
pio run -t upload
pio device monitor
```

Choose the correct serial port if PlatformIO cannot identify it. On first build, PlatformIO downloads the ESP32 platform and libraries. Environments are `esp32-c3-supermini`, `esp32-devkit`, `esp32-devkit-classic-bt`, and `esp32-s3-devkit`. Edit the matching environment's `OLED_SDA_PIN` and `OLED_SCL_PIN` build flags in `platformio.ini` to set pins for a local custom build.

## BLE & Bluetooth Classic text protocol

- Advertised device name: `ESP32-OLED`
- **Nordic UART Service (BLE)**:
  - Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  - RX Characteristic (Phone writes to ESP32): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
  - TX Characteristic (ESP32 notifies Phone): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- **Bluetooth Classic SPP**: Standard RFCOMM serial profile (`00001101-0000-1000-8000-00805F9B34FB`)
- Compatible with mobile serial terminal apps (e.g. *Serial Bluetooth Terminal*, *nRF Connect*), the Web Bluetooth Studio, and the companion Android app.

## Google Maps OLED Navigation (Android Companion App)

The `android/` directory contains a native Android companion app that extracts real-time navigation turn directions and telemetry from Google Maps (`com.google.android.apps.maps`) and renders them onto the SSD1306 128x64 OLED.

### Features
- **Notification Interception**: Uses `NotificationListenerService` to parse turn maneuvers, distance to turn, street names, ETA, and remaining distance directly from Google Maps notifications.
- **Monochrome 32x32 Turn Icon Generator**: Thresholds and converts turn maneuver arrows into 1-bit 32x32 monochrome bitmaps (128 bytes) compatible with Adafruit GFX `drawBitmap()`, encoded in Base64.
- **Real-Time Speedometer**: Integrates phone GPS location provider to stream live vehicle speed (km/h) to the display HUD.
- **Universal Dual-Transport**: Connects over both **Bluetooth Classic (SPP)** (for standard ESP32) and **BLE (Nordic UART Service)** with MTU negotiation and chunked writes (for ESP32-C3 SuperMini and ESP32-S3).
- **Interactive Desk Testing**: Built-in test buttons (Left, Right, Straight, Roundabout, Clear) and live 128x64 OLED HUD preview to test display layouts without driving.

### Building & Running Android App
1. Open the `android/` folder in Android Studio (or run `./gradlew assembleDebug`).
2. Grant Notification Access, Bluetooth, and Location permissions when prompted.
3. Tap **Pick Device** and select your paired ESP32 or discoverable BLE device (`ESP32-OLED`).
4. Start navigation in Google Maps or use the quick test buttons to simulate maneuvers.

## GitHub Pages flasher

The workflow at `.github/workflows/build-and-deploy.yml` builds firmware on pushes to `main` and publishes a board/transport selector on GitHub Pages. Choices are prebuilt: C3 SuperMini BLE (GPIO 8/9), classic ESP32 BLE or Bluetooth Classic (GPIO 21/22), and ESP32-S3 BLE (GPIO 8/9). The page displays the abbreviated commit used for the deployment. Enable Pages in repository Settings with **GitHub Actions** as the build/deployment source. Connect the board over USB and select its matching image to install.

The page cannot compile arbitrary pins: change the environment's pin flags in `platformio.ini` and build locally for other GPIO pairs. Manifests use bootloader offset `0x0`, partition-table offset `0x8000`, and application offset `0x10000`.

## Notes

- OLED rendering requires the display to be wired and detected at the configured I2C address.
- Browser BLE sender and USB firmware installer are separate operations; BLE cannot flash the ESP32.
- No physical hardware test has been performed as part of this repository setup.