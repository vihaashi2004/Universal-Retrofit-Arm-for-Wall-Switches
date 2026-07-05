#pragma once
#include <Arduino.h>

// =====================================================================
// Inverse / Forward Kinematics for the Five-Bar Linkage
// =====================================================================
// Motor 1: (0, 0)
// Motor 2: (L5, 0)
// L1/L2 = proximal links, L3/L4 = distal links.
// Uses the elbow-up solution.

namespace kinematics {

bool inverseKinematics(float xp, float yp, float &q1_deg, float &q2_deg);
void forwardKinematics(float q1_deg, float q2_deg, float &xp, float &yp);
bool isInWorkspace(float xp, float yp);

} // namespace kinematics
