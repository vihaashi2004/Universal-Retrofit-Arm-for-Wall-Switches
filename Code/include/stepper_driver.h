#pragma once
#include <Arduino.h>

// Motor indices used throughout the firmware:
//   MOTOR_1 = 0
//   MOTOR_2 = 1

enum MotorId : uint8_t { MOTOR_1 = 0, MOTOR_2 = 1 };

namespace stepper_driver {

// Initializes GPIO + UART + TMC2209 registers for both drivers.
void init();

// EN pin control (active LOW at the driver, this wraps that polarity).
void enable();
void disable();

// Sets the RMS current (mA) on a single driver.
void setCurrent(uint8_t motor, uint16_t mA);

// Non-blocking coordinated move to an absolute angle (degrees).
void moveToAngle(uint8_t motor, float angleDeg);

// Directly spins a motor at a constant speed in a given direction —
// used only by the homing routine.
void jogContinuous(uint8_t motor, bool clockwise, float rpm);
void stopContinuous(uint8_t motor);

// Sets the *logical* current angle without moving.
void setCurrentAngle(uint8_t motor, float angleDeg);

float getCurrentAngle(uint8_t motor);

bool isMoving(uint8_t motor);

// Immediately halts step pulse generation on both motors.
void emergencyStop();

// Returns true if UART communication to the given motor's TMC2209 was
// established successfully during init().
bool isUartOk(uint8_t motor);

// StallGuard configuration — call after init() to tune sensitivity.
// threshold: 0 = most sensitive (stalls easily), 255 = least sensitive.
void setStallGuardThreshold(uint8_t motor, uint8_t threshold);

// Reads the live StallGuard result register (0-510). Lower = closer to stall.
// Useful for calibrating SGTHRS via Serial Monitor.
uint16_t getStallGuardResult(uint8_t motor);

} // namespace stepper_driver