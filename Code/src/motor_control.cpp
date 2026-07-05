#include "motor_control.h"
#include "config.h"
#include "pins.h"
#include "stepper_driver.h"

AccelStepper stepper1(
  AccelStepper::DRIVER,
  pins::M1_STEP,
  pins::M1_DIR
);

AccelStepper stepper2(
  AccelStepper::DRIVER,
  pins::M2_STEP,
  pins::M2_DIR
);

bool motor1ObstacleStopped = false;
bool motor2ObstacleStopped = false;

// Confirm obstacle with 3 consecutive HIGH readings.
static bool obstacleConfirmedHigh(uint8_t pin) {
  uint8_t highCount = 0;

  for (uint8_t i = 0; i < 3; i++) {
    if (digitalRead(pin) == HIGH) highCount++;
    delay(2);
  }

  return highCount == 3;
}

static void hardStopOneMotor(uint8_t motor) {
  stepper_driver::stopContinuous(motor);

  if (motor == MOTOR_1) {
    motor1ObstacleStopped = true;

    long pos = stepper1.currentPosition();
    stepper1.setSpeed(0);
    stepper1.moveTo(pos);
    stepper1.setCurrentPosition(pos);

    digitalWrite(pins::M1_EN, HIGH); // TMC2209 disable, active LOW
    Serial.println("[motor] Motor 1 stopped by obstacle");
  }
  else if (motor == MOTOR_2) {
    motor2ObstacleStopped = true;

    long pos = stepper2.currentPosition();
    stepper2.setSpeed(0);
    stepper2.moveTo(pos);
    stepper2.setCurrentPosition(pos);

    digitalWrite(pins::M2_EN, HIGH); // TMC2209 disable, active LOW
    Serial.println("[motor] Motor 2 stopped by obstacle");
  }
}

void setupMotors() {
  pinMode(pins::M1_EN, OUTPUT);
  pinMode(pins::M2_EN, OUTPUT);

  pinMode(pins::M1_DIAG, INPUT);
  pinMode(pins::M2_DIAG, INPUT);

  digitalWrite(pins::M1_EN, LOW);
  digitalWrite(pins::M2_EN, LOW);

  stepper1.setMaxSpeed(motorcfg::MOTOR_MAX_SPEED);
  stepper1.setAcceleration(motorcfg::MOTOR_ACCELERATION);

  stepper2.setMaxSpeed(motorcfg::MOTOR_MAX_SPEED);
  stepper2.setAcceleration(motorcfg::MOTOR_ACCELERATION);

  stepper1.setCurrentPosition(0);
  stepper2.setCurrentPosition(0);

  motor1ObstacleStopped = false;
  motor2ObstacleStopped = false;

  Serial.println("Motors initialized");
}

void resetMotorObstacleStops() {
  motor1ObstacleStopped = false;
  motor2ObstacleStopped = false;

  digitalWrite(pins::M1_EN, LOW);
  digitalWrite(pins::M2_EN, LOW);

  Serial.println("[motor] obstacle stop flags reset");
}

void runMotors() {
  // Legacy mode: old automatic back-and-forth run.
  if (motorsRunning) {
    if (!motor1ObstacleStopped && obstacleConfirmedHigh(pins::M1_DIAG)) {
      hardStopOneMotor(MOTOR_1);
    }

    if (!motor2ObstacleStopped && obstacleConfirmedHigh(pins::M2_DIAG)) {
      hardStopOneMotor(MOTOR_2);
    }

    if (motor1ObstacleStopped && motor2ObstacleStopped) {
      motorsRunning = false;
      Serial.println("[motor] both motors stopped by obstacles");
      return;
    }

    if (!motor1ObstacleStopped) {
      if (stepper1.distanceToGo() == 0) {
        stepper1.moveTo(stepper1.currentPosition() == 0 ? motorcfg::MOTOR_TRAVEL_STEPS : 0);
      }
      stepper1.run();
    }

    if (!motor2ObstacleStopped) {
      if (stepper2.distanceToGo() == 0) {
        stepper2.moveTo(stepper2.currentPosition() == 0 ? motorcfg::MOTOR_TRAVEL_STEPS : 0);
      }
      stepper2.run();
    }

    return;
  }

  // Position-control mode: run the target angles set by position_control.cpp.
  // This is the important part for the Switches UI jog buttons and saved ON/OFF positions.
  if (!motor1ObstacleStopped && stepper1.distanceToGo() != 0) {
    if (obstacleConfirmedHigh(pins::M1_DIAG)) {
      hardStopOneMotor(MOTOR_1);
    }
  }

  if (!motor2ObstacleStopped && stepper2.distanceToGo() != 0) {
    if (obstacleConfirmedHigh(pins::M2_DIAG)) {
      hardStopOneMotor(MOTOR_2);
    }
  }

  if (!motor1ObstacleStopped) stepper1.run();
  if (!motor2ObstacleStopped) stepper2.run();
}
