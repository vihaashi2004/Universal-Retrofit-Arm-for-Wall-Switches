#include "stepper_driver.h"
#include "pins.h"
#include "motor_control.h"

#include <AccelStepper.h>
#include <TMCStepper.h>
#include <SoftwareSerial.h>

// =====================================================
// Single-wire half-duplex UART using EspSoftwareSerial.
// This is required on ESP32 because HardwareSerial does not cleanly
// support single-pin (same RX/TX) half-duplex mode.
// =====================================================

namespace stepper_driver {

static EspSoftwareSerial::UART uartM1;
static EspSoftwareSerial::UART uartM2;

static TMC2209Stepper driverM1(&uartM1, pins::TMC_R_SENSE, pins::TMC_DRIVER_ADDR);
static TMC2209Stepper driverM2(&uartM2, pins::TMC_R_SENSE, pins::TMC_DRIVER_ADDR);

static bool s_enabled = false;
static bool s_uartOk[2] = { false, false };

static volatile bool s_jogging[2] = { false, false };
static float s_jogSpeedStepsPerSec[2] = { 0.0f, 0.0f };

static constexpr float DEFAULT_MAX_SPEED = 1200.0f;
static constexpr float DEFAULT_ACCEL     = 600.0f;

static constexpr uint8_t DEFAULT_SGTHRS = 100;

static float rpmToStepsPerSecond(float rpm) {
  return (rpm * mech::STEPS_PER_REV * mech::MICROSTEPS) / 60.0f;
}

static AccelStepper* getStepper(uint8_t motor) {
  if (motor == MOTOR_1) return &stepper1;
  if (motor == MOTOR_2) return &stepper2;
  return nullptr;
}

static TMC2209Stepper* getDriver(uint8_t motor) {
  if (motor == MOTOR_1) return &driverM1;
  if (motor == MOTOR_2) return &driverM2;
  return nullptr;
}

static void stepperTask(void* parameter) {
  while (true) {
    if (s_enabled) {
      // This task is only for continuous jogging during homing.
      // Normal START motion is handled in runMotors() from loop().
      // Do not call stepper.run() from two places at the same time.
      if (s_jogging[MOTOR_1]) {
        stepper1.setSpeed(s_jogSpeedStepsPerSec[MOTOR_1]);
        stepper1.runSpeed();
      }

      if (s_jogging[MOTOR_2]) {
        stepper2.setSpeed(s_jogSpeedStepsPerSec[MOTOR_2]);
        stepper2.runSpeed();
      }
    }

    delay(1);
  }
}

// =====================================================
// Configure one TMC2209 for StallGuard-based stall detection.
// =====================================================
static bool configureDriver(TMC2209Stepper* driver, const char* label, uint16_t mA) {
  driver->begin();

  driver->pdn_disable(true);
  driver->I_scale_analog(false);

  driver->mstep_reg_select(true);
  driver->microsteps(mech::MICROSTEPS);

  driver->rms_current(mA);

  // SpreadCycle needed for StallGuard/DIAG to work reliably.
  driver->en_spreadCycle(true);

  driver->TCOOLTHRS(0xFFFFF);
  driver->SGTHRS(DEFAULT_SGTHRS);
  driver->semin(0);

  delay(20);

  uint8_t result = driver->test_connection();

  Serial.print("[stepper_driver] ");
  Serial.print(label);
  Serial.print(" test_connection() = ");
  Serial.print(result);
  Serial.println(result == 0 ? " (OK)" : " (FAILED)");

  // Extra sanity check: read IFCNT (interface transmission counter).
  // If UART is working, this should be readable without error.
  uint32_t ifcnt = driver->IFCNT();
  Serial.print("[stepper_driver] ");
  Serial.print(label);
  Serial.print(" IFCNT = ");
  Serial.println(ifcnt);

  return (result == 0);
}

void init() {
  pinMode(pins::M1_EN, OUTPUT);
  pinMode(pins::M2_EN, OUTPUT);

  disable();

  stepper1.setMaxSpeed(DEFAULT_MAX_SPEED);
  stepper1.setAcceleration(DEFAULT_ACCEL);

  stepper2.setMaxSpeed(DEFAULT_MAX_SPEED);
  stepper2.setAcceleration(DEFAULT_ACCEL);

  // Single-wire half duplex: same pin used for RX and TX.
  uartM1.begin(pins::TMC_BAUD, SWSERIAL_8N1, pins::M1_UART, pins::M1_UART, false);
  uartM2.begin(pins::TMC_BAUD, SWSERIAL_8N1, pins::M2_UART, pins::M2_UART, false);

  uartM1.enableIntTx(false);
  uartM2.enableIntTx(false);

  delay(100);

  s_uartOk[MOTOR_1] = configureDriver(&driverM1, "Motor 1", 900);
  s_uartOk[MOTOR_2] = configureDriver(&driverM2, "Motor 2", 900);

  Serial.print("[stepper_driver] Motor 1 UART: ");
  Serial.println(s_uartOk[MOTOR_1] ? "OK" : "FAILED (standalone mode)");

  Serial.print("[stepper_driver] Motor 2 UART: ");
  Serial.println(s_uartOk[MOTOR_2] ? "OK" : "FAILED (standalone mode)");

  xTaskCreatePinnedToCore(
    stepperTask,
    "StepperTask",
    4096,
    nullptr,
    1,
    nullptr,
    1
  );

  Serial.println("[stepper_driver] adapter initialized (TMCStepper + StallGuard)");
}

void enable() {
  digitalWrite(pins::M1_EN, LOW);
  digitalWrite(pins::M2_EN, LOW);
  s_enabled = true;
}

void disable() {
  s_enabled = false;

  s_jogging[MOTOR_1] = false;
  s_jogging[MOTOR_2] = false;

  digitalWrite(pins::M1_EN, HIGH);
  digitalWrite(pins::M2_EN, HIGH);
}

void setCurrent(uint8_t motor, uint16_t mA) {
  TMC2209Stepper* driver = getDriver(motor);
  if (driver == nullptr) return;

  if (!s_uartOk[motor]) {
    Serial.print("[stepper_driver] setCurrent skipped, UART not OK for motor ");
    Serial.println(motor);
    return;
  }

  driver->rms_current(mA);
}

void setStallGuardThreshold(uint8_t motor, uint8_t threshold) {
  TMC2209Stepper* driver = getDriver(motor);
  if (driver == nullptr) return;
  if (!s_uartOk[motor]) return;

  driver->SGTHRS(threshold);

  Serial.print("[stepper_driver] SGTHRS set for motor ");
  Serial.print(motor);
  Serial.print(" = ");
  Serial.println(threshold);
}

uint16_t getStallGuardResult(uint8_t motor) {
  TMC2209Stepper* driver = getDriver(motor);
  if (driver == nullptr) return 0;
  if (!s_uartOk[motor]) return 0;

  return driver->SG_RESULT();
}

void moveToAngle(uint8_t motor, float angleDeg) {
  AccelStepper* stepper = getStepper(motor);
  if (stepper == nullptr) return;

  long targetSteps = static_cast<long>(angleDeg * mech::STEPS_PER_DEG);

  s_jogging[motor] = false;
  stepper->moveTo(targetSteps);
}

void jogContinuous(uint8_t motor, bool clockwise, float rpm) {
  AccelStepper* stepper = getStepper(motor);
  if (stepper == nullptr) return;

  float speed = rpmToStepsPerSecond(rpm);
  if (!clockwise) speed = -speed;

  s_jogSpeedStepsPerSec[motor] = speed;
  s_jogging[motor] = true;

  stepper->setSpeed(speed);
}

void stopContinuous(uint8_t motor) {
  AccelStepper* stepper = getStepper(motor);
  if (stepper == nullptr) return;

  s_jogging[motor] = false;
  s_jogSpeedStepsPerSec[motor] = 0.0f;

  long currentPos = stepper->currentPosition();
  stepper->setSpeed(0);
  stepper->moveTo(currentPos);
  stepper->setCurrentPosition(currentPos);

  Serial.print("[stepper_driver] Motor stopped: ");
  Serial.println(motor);
}

void setCurrentAngle(uint8_t motor, float angleDeg) {
  AccelStepper* stepper = getStepper(motor);
  if (stepper == nullptr) return;

  long currentSteps = static_cast<long>(angleDeg * mech::STEPS_PER_DEG);
  stepper->setCurrentPosition(currentSteps);
}

float getCurrentAngle(uint8_t motor) {
  AccelStepper* stepper = getStepper(motor);
  if (stepper == nullptr) return 0.0f;

  return stepper->currentPosition() / mech::STEPS_PER_DEG;
}

bool isMoving(uint8_t motor) {
  AccelStepper* stepper = getStepper(motor);
  if (stepper == nullptr) return false;

  if (s_jogging[motor]) return true;
  return stepper->distanceToGo() != 0;
}

void emergencyStop() {
  s_jogging[MOTOR_1] = false;
  s_jogging[MOTOR_2] = false;

  long pos1 = stepper1.currentPosition();
  long pos2 = stepper2.currentPosition();

  stepper1.setSpeed(0);
  stepper2.setSpeed(0);

  stepper1.moveTo(pos1);
  stepper2.moveTo(pos2);

  stepper1.setCurrentPosition(pos1);
  stepper2.setCurrentPosition(pos2);

  Serial.println("[stepper_driver] Emergency hard stop triggered");
}

bool isUartOk(uint8_t motor) {
  if (motor > MOTOR_2) return false;
  return s_uartOk[motor];
}

} // namespace stepper_driver