#include "position_control.h"

#include "homing.h"
#include "kinematics.h"
#include "motor_control.h"
#include "pins.h"
#include "stepper_driver.h"

namespace position_control {

static float currentX = 0.0f;
static float currentY = 0.0f;
static float targetX = 0.0f;
static float targetY = 0.0f;

static float targetQ1 = 0.0f;
static float targetQ2 = 0.0f;

static bool moving = false;
static String s_lastError = "";

static bool s_pendingActuation = false;
static bool s_solenoidActive = false;
static uint32_t s_solenoidStartMs = 0;
static constexpr uint32_t SOLENOID_DURATION_MS = 200;

static void updateXYFromAngles() {
  float q1 = stepper_driver::getCurrentAngle(MOTOR_1);
  float q2 = stepper_driver::getCurrentAngle(MOTOR_2);
  kinematics::forwardKinematics(q1, q2, currentX, currentY);
}

void init() {
  currentX = 0.0f;
  currentY = 0.0f;
  targetX = 0.0f;
  targetY = 0.0f;
  targetQ1 = 0.0f;
  targetQ2 = 0.0f;
  moving = false;
  s_lastError = "";
  s_pendingActuation = false;
  s_solenoidActive = false;

  pinMode(pins::SOLENOID, OUTPUT);
  digitalWrite(pins::SOLENOID, LOW);

  Serial.println("[position] init complete");
}

void syncFromMotorAngles() {
  updateXYFromAngles();
  targetX = currentX;
  targetY = currentY;
  moving = false;

  Serial.print("[position] synced from motors: X=");
  Serial.print(currentX, 2);
  Serial.print(" Y=");
  Serial.println(currentY, 2);
}

void setCurrent(float x, float y) {
  currentX = x;
  currentY = y;
  targetX = x;
  targetY = y;
  moving = false;

  Serial.print("[position] logical position set: X=");
  Serial.print(currentX, 2);
  Serial.print(" Y=");
  Serial.println(currentY, 2);
}

bool moveToXY(float x, float y) {
  float q1, q2;
  if (!kinematics::inverseKinematics(x, y, q1, q2)) {
    s_lastError = "Target outside five-bar workspace";
    Serial.print("[position] rejected unreachable target: X=");
    Serial.print(x, 2);
    Serial.print(" Y=");
    Serial.println(y, 2);
    return false;
  }

  resetMotorObstacleStops();
  stepper_driver::enable();

  targetX = x;
  targetY = y;
  targetQ1 = q1;
  targetQ2 = q2;

  Serial.print("[position] move X=");
  Serial.print(targetX, 2);
  Serial.print(" Y=");
  Serial.print(targetY, 2);
  Serial.print(" -> Q1=");
  Serial.print(targetQ1, 2);
  Serial.print(" Q2=");
  Serial.println(targetQ2, 2);

  // AccelStepper allows retargeting while already moving, so this gives
  // smooth real-time jog behavior instead of rejecting as “busy”.
  stepper_driver::moveToAngle(MOTOR_1, targetQ1);
  stepper_driver::moveToAngle(MOTOR_2, targetQ2);

  moving = true;
  s_pendingActuation = false; // Reset by default, pressAtXY will set it.
  s_lastError = "";
  return true;
}

bool pressAtXY(float x, float y) {
  if (moveToXY(x, y)) {
    s_pendingActuation = true;
    return true;
  }
  return false;
}

bool jogXY(float dx, float dy) {
  // Use live estimated position, not stale target.
  updateXYFromAngles();
  return moveToXY(currentX + dx, currentY + dy);
}

void update() {
  updateXYFromAngles();

  if (s_solenoidActive) {
    if (millis() - s_solenoidStartMs > SOLENOID_DURATION_MS) {
      digitalWrite(pins::SOLENOID, LOW);
      s_solenoidActive = false;
      Serial.println("[position] Solenoid released.");
    }
  }

  if (!moving)
    return;

  if (motor1ObstacleStopped || motor2ObstacleStopped) {
    moving = false;
    s_pendingActuation = false;
    s_lastError = "Obstacle detected during position move";
    Serial.println("[position] stopped: obstacle detected");
    return;
  }

  if (!stepper_driver::isMoving(MOTOR_1) &&
      !stepper_driver::isMoving(MOTOR_2)) {
    moving = false;
    s_lastError = "";

    Serial.print("[position] move complete: X=");
    Serial.print(currentX, 2);
    Serial.print(" Y=");
    Serial.println(currentY, 2);

    if (s_pendingActuation) {
      s_pendingActuation = false;
      s_solenoidActive = true;
      s_solenoidStartMs = millis();
      digitalWrite(pins::SOLENOID, HIGH);
      Serial.println("[position] Target reached! Actuating solenoid.");
    }
  }
}

float getX() { return currentX; }
float getY() { return currentY; }
float getQ1() { return stepper_driver::getCurrentAngle(MOTOR_1); }
float getQ2() { return stepper_driver::getCurrentAngle(MOTOR_2); }

bool isBusy() {
  return moving || stepper_driver::isMoving(MOTOR_1) ||
         stepper_driver::isMoving(MOTOR_2);
}

const String &lastError() { return s_lastError; }

} // namespace position_control
