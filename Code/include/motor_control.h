#pragma once
#include <Arduino.h>
#include <AccelStepper.h>

extern AccelStepper stepper1;
extern AccelStepper stepper2;

extern bool motorsRunning;

extern bool motor1ObstacleStopped;
extern bool motor2ObstacleStopped;

void setupMotors();
void runMotors();

void resetMotorObstacleStops();