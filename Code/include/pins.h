#pragma once
#include <Arduino.h>

// =====================================================================
// Pin Definitions — ESP32 Five-Bar Linkage Switch Pressing Controller
// =====================================================================
//
// NOTE (Motor UART Topology): The two TMC2209 drivers are wired to two
// SEPARATE PDN_UART pins (GPIO22 for Motor 1, GPIO25 for Motor 2). This
// firmware therefore uses two independent HardwareSerial ports (UART1 /
// UART2), each half-duplex through a 1k series resistor to one driver.
// If your hardware instead ties both drivers to a single shared UART
// bus, you must re-wire stepper_driver.cpp to use MS1/MS2 address pins
// on one shared HardwareSerial port instead.
//
// NOTE (GPIO34): GPIO34 is input-only and has no internal pull-up/down.
// It is only ever used here as a plain INPUT (StallGuard DIAG read for
// Motor 2). If your TMC2209's DIAG output is open-drain, add an external
// ~10k pull-down resistor on this pin.
// =====================================================================

namespace pins {

// --- Motor 1 ---
constexpr uint8_t M1_DIR   = 4;
constexpr uint8_t M1_STEP  = 16;
constexpr uint8_t M1_EN    = 17;   // Active LOW
constexpr uint8_t M1_DIAG  = 21;   // StallGuard output, interrupt-capable
constexpr uint8_t M1_UART  = 22;   // PDN_UART, half-duplex via 1k resistor

// --- Motor 2 ---
constexpr uint8_t M2_DIR   = 12;
constexpr uint8_t M2_STEP  = 27;
constexpr uint8_t M2_EN    = 18;   // Active LOW
constexpr uint8_t M2_DIAG  = 34;   // Input-only, no pull-up/down, poll only
constexpr uint8_t M2_UART  = 25;   // PDN_UART, half-duplex via 1k resistor

// --- Solenoid ---
constexpr uint8_t SOLENOID = 13;   // MOSFET gate, JF-0530B

// --- TMC2209 UART ---
constexpr uint32_t TMC_BAUD        = 115200;
constexpr uint8_t  TMC_DRIVER_ADDR = 0b00;
constexpr float    TMC_R_SENSE     = 0.11f;

} // namespace pins

// =====================================================================
// Mechanical / motion constants
// =====================================================================
namespace mech {

// 1.8 degree stepper motor = 200 full steps per revolution
constexpr uint16_t STEPS_PER_REV = 200;

// IMPORTANT:
// කලින් මෙතන 16 තිබුණා.
// 180 degrees command කළාම 360 degrees වගේ කැරකුණ නිසා,
// actual driver microstep setting එක 8 වගේ behave වෙනවා.
// ඒ නිසා 8 කරලා තියෙනවා.
constexpr uint16_t MICROSTEPS = 8;

// Steps needed for 1 degree.
// 200 * 8 = 1600 steps per 360 degrees
// 1600 / 360 = 4.444 steps per degree
constexpr float STEPS_PER_DEG =
    (STEPS_PER_REV * MICROSTEPS) / 360.0f;

} // namespace mech