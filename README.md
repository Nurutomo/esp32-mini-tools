# ESP32-C3 Mini Tools

PlatformIO firmware and a GitHub Pages web flasher for an ESP32-C3 SuperMini with a wired 128x64 SSD1306 I2C OLED. The device advertises a BLE GATT service; write UTF-8 text to its characteristic to display it.

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

Choose the correct serial port if PlatformIO cannot identify it. On first build, PlatformIO downloads the ESP32 platform and libraries. The configured environment is `esp32-c3-supermini` and uses the `esp32-c3-devkitm-1` PlatformIO board definition.

## BLE text protocol

- Advertised device name: `ESP32-C3-OLED`
- Service UUID: `8b5d0001-6e8f-4c2a-9b73-6d414c454001`
- Write characteristic UUID: `8b5d0002-6e8f-4c2a-9b73-6d414c454001`
- Write UTF-8 bytes, up to 20 bytes per message (the default BLE ATT payload); each write replaces the displayed text.

Use the BLE sender on the Pages site or a BLE GATT client app such as nRF Connect. The browser sender requires Web Bluetooth support (typically Chromium) and HTTPS or localhost. The web flasher uses Web Serial and likewise needs a supported Chromium browser and secure context.

## GitHub Pages flasher

The workflow at `.github/workflows/build-and-deploy.yml` builds firmware on pushes to `main` and deploys `web/index.html` plus the ESP32-C3 binaries to GitHub Pages. Enable Pages in repository Settings with **GitHub Actions** as the build/deployment source. After the first successful workflow run, open the Pages URL, connect the board over USB, and install.

The generated ESP32-C3 manifest uses bootloader offset `0x0`, partition-table offset `0x8000`, and application offset `0x10000`, matching the standard ESP-IDF/PlatformIO image layout.

## Notes

- OLED rendering requires the display to be wired and detected at the configured I2C address.
- Browser BLE sender and USB firmware installer are separate operations; BLE cannot flash the ESP32.
- No physical hardware test has been performed as part of this repository setup.