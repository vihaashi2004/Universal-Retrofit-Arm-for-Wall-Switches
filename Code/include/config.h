#pragma once
#include <Arduino.h>

// ---------------- WiFi Credentials ----------------
#define WIFI_SSID "Harendra"
#define WIFI_PASSWORD "Harendra1"

// ---------------- Motor Motion Config ----------------
namespace motorcfg {

constexpr uint16_t RMS_CURRENT_MA = 900;
constexpr uint8_t HOMING_CURRENT_PCT = 70;

constexpr float HOMING_RPM = 8.0f;
constexpr float HOMING_BACKOFF_DEG = 5.0f;

constexpr uint32_t HOMING_TIMEOUT_MS = 15000;
constexpr uint32_t MOVE_TIMEOUT_MS = 8000;

// Normal motor running values
constexpr float MOTOR_MAX_SPEED = 1000.0f;
constexpr float MOTOR_ACCELERATION = 500.0f;
constexpr long MOTOR_TRAVEL_STEPS = 2000;

} // namespace motorcfg

// ---------------- Home Angle Config ----------------
namespace homecfg {

constexpr float HOME_Q1_DEG = 0.0f;
constexpr float HOME_Q2_DEG = 0.0f;

} // namespace homecfg
// ---------------- Five-bar linkage dimensions ----------------
// IMPORTANT: change these to your real measured values in millimetres.
// UI switch positions are X/Y points in this coordinate system.
namespace linkage {
constexpr float L1 = 80.0f;   // Motor 1 proximal arm length, mm
constexpr float L2 = 80.0f;   // Motor 2 proximal arm length, mm
constexpr float L3 = 120.0f;  // Motor 1 distal arm length, mm
constexpr float L4 = 120.0f;  // Motor 2 distal arm length, mm
constexpr float L5 = 100.0f;  // Distance between motor shafts, mm
}
