#pragma once
#include <Arduino.h>

namespace position_control {

void init();

// Call this after homing. It reads the two motor angles and calculates
// the real end-effector X/Y by forward kinematics.
void syncFromMotorAngles();

// Optional manual logical reset.
void setCurrent(float x, float y);

// Absolute move to a switch teaching point. (No solenoid punch)
bool moveToXY(float x, float y);

// Command a move to press a switch, actuating the solenoid upon arrival.
bool pressAtXY(float x, float y);

// Relative move from current X/Y. Used by the UI arrow buttons.
bool jogXY(float dx, float dy);

// Must be called in loop(). Keeps X/Y live while motors are moving.
void update();

float getX();
float getY();
float getQ1();
float getQ2();

bool isBusy();
const String &lastError();

} // namespace position_control
