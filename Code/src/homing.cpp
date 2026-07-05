#include "homing.h"
#include "pins.h"
#include "stepper_driver.h"

namespace homing {

static bool s_homed = false;

// =====================================================
// HOMING TEST SETTINGS
// =====================================================

// Speed eka adu/wadi karanna meka wenas karanna
static constexpr float HOMING_RPM = 2.0f;

// Obstacle detect nowunoth motor ekak maximum karakenna puluwan angle eka
static constexpr float MAX_ROTATE_DEG = 50.0f;

// Safety timeout
static constexpr uint32_t HOMING_TIMEOUT_MS = 20000;

// Logical home angle
static constexpr float HOME_Q1_DEG = 0.0f;
static constexpr float HOME_Q2_DEG = 0.0f;

// =====================================================
// Init
// =====================================================
void init() {
  s_homed = false;

  pinMode(pins::M1_DIAG, INPUT);
  pinMode(pins::M2_DIAG, INPUT);

  pinMode(pins::M1_EN, OUTPUT);
  pinMode(pins::M2_EN, OUTPUT);

  // Start disabled
  digitalWrite(pins::M1_EN, HIGH);
  digitalWrite(pins::M2_EN, HIGH);

  Serial.println("[homing] init complete");
}

// =====================================================
// Homed state
// =====================================================
bool isHomed() { return s_homed; }

// =====================================================
// DIAG debounce
// Obstacle confirm wenna consecutive HIGH 3k balanna
// =====================================================
static bool diagConfirmedHigh(uint8_t pin) {
  uint8_t highCount = 0;

  for (uint8_t i = 0; i < 3; i++) {
    if (digitalRead(pin) == HIGH) {
      highCount++;
    }

    delay(3);
  }

  return highCount == 3;
}

// =====================================================
// Check max angle
// =====================================================
static bool reachedMaxRotation(uint8_t motor) {
  float angle = stepper_driver::getCurrentAngle(motor);

  if (angle < 0) {
    angle = -angle;
  }

  return angle >= MAX_ROTATE_DEG;
}

// =====================================================
// HARD stop one motor only
// Stops software jogging AND disables that motor's driver EN pin.
// =====================================================
static void hardStopMotor(uint8_t motor) {
  stepper_driver::stopContinuous(motor);

  if (motor == MOTOR_1) {
    digitalWrite(pins::M1_EN, HIGH); // TMC2209 disable, active LOW
    Serial.println("[homing] Motor 1 HARD STOPPED");
  } else if (motor == MOTOR_2) {
    digitalWrite(pins::M2_EN, HIGH); // TMC2209 disable, active LOW
    Serial.println("[homing] Motor 2 HARD STOPPED");
  }
}

// =====================================================
// Full homing process
// =====================================================
Result home() {
  Serial.println();
  Serial.println("[homing] ===============================");
  Serial.println("[homing] obstacle detect homing started");
  Serial.println("[homing] ===============================");

  s_homed = false;

  bool m1Done = false;
  bool m2Done = false;

  bool m1ObstacleDetected = false;
  bool m2ObstacleDetected = false;

  bool m1LimitReached = false;
  bool m2LimitReached = false;

  bool m1Timeout = false;
  bool m2Timeout = false;

  // IMPORTANT:
  // Use stepper_driver::enable(), not only digitalWrite EN LOW.
  // This enables internal step generation too.
  stepper_driver::enable();

  delay(300);

  // Reset logical angles before test
  stepper_driver::setCurrentAngle(MOTOR_1, 0.0f);
  stepper_driver::setCurrentAngle(MOTOR_2, 0.0f);

  delay(100);

  Serial.println("[homing] both motors starting clockwise");

  stepper_driver::jogContinuous(MOTOR_1, true, HOMING_RPM);
  stepper_driver::jogContinuous(MOTOR_2, true, HOMING_RPM);

  uint32_t startTime = millis();

  while (!m1Done || !m2Done) {

    // =================================================
    // Motor 1 obstacle check
    // =================================================
    if (!m1Done) {
      if (digitalRead(pins::M1_DIAG) == HIGH) {
        if (diagConfirmedHigh(pins::M1_DIAG)) {
          m1ObstacleDetected = true;
          m1Done = true;

          Serial.println("[homing] Motor 1 obstacle detected");
          hardStopMotor(MOTOR_1);
        }
      }

      if (!m1Done && reachedMaxRotation(MOTOR_1)) {
        m1LimitReached = true;
        m1Done = true;

        Serial.println("[homing] Motor 1 reached 180 degree limit");
        hardStopMotor(MOTOR_1);
      }
    }

    // =================================================
    // Motor 2 obstacle check
    // =================================================
    if (!m2Done) {
      if (digitalRead(pins::M2_DIAG) == HIGH) {
        if (diagConfirmedHigh(pins::M2_DIAG)) {
          m2ObstacleDetected = true;
          m2Done = true;

          Serial.println("[homing] Motor 2 obstacle detected");
          hardStopMotor(MOTOR_2);
        }
      }

      if (!m2Done && reachedMaxRotation(MOTOR_2)) {
        m2LimitReached = true;
        m2Done = true;

        Serial.println("[homing] Motor 2 reached 180 degree limit");
        hardStopMotor(MOTOR_2);
      }
    }

    // =================================================
    // Safety timeout
    // =================================================
    if (millis() - startTime > HOMING_TIMEOUT_MS) {
      if (!m1Done) {
        m1Timeout = true;
        m1Done = true;

        Serial.println("[homing] Motor 1 timeout");
        hardStopMotor(MOTOR_1);
      }

      if (!m2Done) {
        m2Timeout = true;
        m2Done = true;

        Serial.println("[homing] Motor 2 timeout");
        hardStopMotor(MOTOR_2);
      }
    }

    delay(2);
  }

  delay(300);

  // Final safety stop
  hardStopMotor(MOTOR_1);
  hardStopMotor(MOTOR_2);

  stepper_driver::disable();

  Serial.print("[homing] Final M1_DIAG = ");
  Serial.println(digitalRead(pins::M1_DIAG));

  Serial.print("[homing] Final M2_DIAG = ");
  Serial.println(digitalRead(pins::M2_DIAG));

  // =====================================================
  // Result check
  // =====================================================
  if (m1Timeout || m1LimitReached || !m1ObstacleDetected) {
    Serial.println("[homing] FAILED: Motor 1 obstacle not detected");
    return Result::TIMEOUT_M1;
  }

  if (m2Timeout || m2LimitReached || !m2ObstacleDetected) {
    Serial.println("[homing] FAILED: Motor 2 obstacle not detected");
    return Result::TIMEOUT_M2;
  }

  // Both obstacles detected successfully
  stepper_driver::setCurrentAngle(MOTOR_1, HOME_Q1_DEG);
  stepper_driver::setCurrentAngle(MOTOR_2, HOME_Q2_DEG);

  s_homed = true;

  Serial.println("[homing] SUCCESS: both motors detected obstacles");
  Serial.println("[homing] homing complete");

  Serial.print("SG1=");
  Serial.print(stepper_driver::getStallGuardResult(MOTOR_1));
  Serial.print(" SG2=");
  Serial.println(stepper_driver::getStallGuardResult(MOTOR_2));

  return Result::OK;
}

// =====================================================
// Manual home override — for use while StallGuard/UART
// obstacle detection is not reliable. Assumes the person has
// already jogged the arm to the physical zero point by hand
// (via the app's jog/X-Y controls) before calling this.
// =====================================================
void forceHomeAtCurrentPosition() {
  stepper_driver::setCurrentAngle(MOTOR_1, HOME_Q1_DEG);
  stepper_driver::setCurrentAngle(MOTOR_2, HOME_Q2_DEG);

  s_homed = true;

  Serial.println("[homing] Manual override: homed at current position "
                 "(obstacle-seek skipped)");
}

} // namespace homing