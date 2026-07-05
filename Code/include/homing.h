#pragma once

namespace homing {

enum class Result {
  OK,
  TIMEOUT_M1,
  TIMEOUT_M2
};

void init();
bool isHomed();
Result home();

// Skips the obstacle-seeking routine entirely. Call this after you've
// manually jogged the linkage to the physical position you want to treat
// as (0,0). It just zeroes both motors' logical angle and marks the
// system as homed — use it when StallGuard/UART isn't reliable yet and
// you still need position control to work.
void forceHomeAtCurrentPosition();

}