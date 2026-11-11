#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

constexpr size_t kMaxBtMessageBytes = 64;
constexpr size_t kMaxPacketBytes = 1024;

typedef void (*PacketReceivedCallback)(const char* packet, size_t len);

// Initialize Bluetooth subsystem (Classic BT on ESP32 Classic target, NimBLE BLE on C3/S3/others)
void initBluetooth(const char* deviceName, PacketReceivedCallback onPacket);

// Periodic update loop (handles chunk timeouts, line flushing, and connection events)
void updateBluetooth();

// Query Bluetooth connection and status
bool isBluetoothConnected();
const char* getBluetoothStatus();
const char* getLastTextMessage();
void setLastTextMessage(const char* msg, size_t len);

// Send text response back to connected client
bool sendBluetoothResponse(const char* text);
