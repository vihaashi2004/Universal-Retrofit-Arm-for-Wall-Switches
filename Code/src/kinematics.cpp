#include "kinematics.h"
#include "config.h"
#include <math.h>

namespace kinematics {

using namespace linkage;

static bool validCos(float &v) {
  // Small tolerance prevents false reject from floating point rounding.
  constexpr float K_EPS = 0.0005f;
  if (v < -1.0f - K_EPS || v > 1.0f + K_EPS)
    return false;
  if (v < -1.0f)
    v = -1.0f;
  if (v > 1.0f)
    v = 1.0f;
  return true;
}

bool inverseKinematics(float xp, float yp, float &q1_deg, float &q2_deg) {
  // --- Motor 1 at origin ---
  float r1 = sqrtf(xp * xp + yp * yp);
  if (r1 < 1e-6f)
    return false;

  float cosA1 = (L1 * L1 + r1 * r1 - L3 * L3) / (2.0f * L1 * r1);
  if (!validCos(cosA1))
    return false;

  float alpha1 = acosf(cosA1);
  float beta1 = atan2f(yp, xp);
  float q1 = beta1 + alpha1;

  // --- Motor 2 offset by L5 on x-axis ---
  float dx2 = xp - L5;
  float r2 = sqrtf(dx2 * dx2 + yp * yp);
  if (r2 < 1e-6f)
    return false;

  float cosA2 = (L2 * L2 + r2 * r2 - L4 * L4) / (2.0f * L2 * r2);
  if (!validCos(cosA2))
    return false;

  float alpha2 = acosf(cosA2);
  float beta2 = atan2f(yp, dx2);
  float q2 = beta2 - alpha2;

  q1_deg = q1 * 180.0f / PI;
  q2_deg = q2 * 180.0f / PI;
  return true;
}

void forwardKinematics(float q1_deg, float q2_deg, float &xp, float &yp) {
  float q1 = q1_deg * PI / 180.0f;
  float q2 = q2_deg * PI / 180.0f;

  float j1x = L1 * cosf(q1);
  float j1y = L1 * sinf(q1);
  float j2x = L5 + L2 * cosf(q2);
  float j2y = L2 * sinf(q2);

  float dx = j2x - j1x;
  float dy = j2y - j1y;
  float d = sqrtf(dx * dx + dy * dy);

  if (d < 1e-6f || d > (L3 + L4) || d < fabsf(L3 - L4)) {
    xp = (j1x + j2x) / 2.0f;
    yp = (j1y + j2y) / 2.0f;
    return;
  }

  float a = (L3 * L3 - L4 * L4 + d * d) / (2.0f * d);
  float hSq = L3 * L3 - a * a;
  float h = hSq > 0.0f ? sqrtf(hSq) : 0.0f;

  float xm = j1x + a * dx / d;
  float ym = j1y + a * dy / d;

  // Upper intersection, matching elbow-up convention.
  xp = xm - h * dy / d;
  yp = ym + h * dx / d;
}

bool isInWorkspace(float xp, float yp) {
  float q1, q2;
  return inverseKinematics(xp, yp, q1, q2);
}

} // namespace kinematics
