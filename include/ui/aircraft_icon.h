#pragma once

#include <cmath>

namespace ui::radar {

/**
 * Top-down airplane silhouette (nose, fuselage, swept wings, tailplane) drawn with filled
 * triangles, rotated to the heading (degrees clockwise from north). The nose tip sits
 * kAircraftNoseLenPx (8 px) ahead of (cx, cy), where the speed vector starts; the tail is
 * ~8 px behind, the wing tips ~7 px to each side.
 *
 * `Gfx` only needs fillTriangle(x0, y0, x1, y1, x2, y2, color).
 */
template <typename Gfx, typename Color>
void drawAircraftIcon(Gfx& gfx, int cx, int cy, float heading_deg, const Color& color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float fx = sinf(rad), fy = -cosf(rad);  // forward on screen (y grows downwards)
  const float rx = cosf(rad), ry = sinf(rad);   // right of forward

  auto px = [&](float f, float r) {
    return cx + static_cast<int>(lroundf(f * fx + r * rx));
  };
  auto py = [&](float f, float r) {
    return cy + static_cast<int>(lroundf(f * fy + r * ry));
  };
  auto tri = [&](float f0, float r0, float f1, float r1, float f2, float r2) {
    gfx.fillTriangle(px(f0, r0), py(f0, r0), px(f1, r1), py(f1, r1), px(f2, r2), py(f2, r2),
                     color);
  };
  auto quad = [&](float f0, float r0, float f1, float r1, float f2, float r2, float f3,
                  float r3) {
    tri(f0, r0, f1, r1, f2, r2);
    tri(f0, r0, f2, r2, f3, r3);
  };

  // Fuselage: pointed nose, then a slim body to the tail.
  tri(8.0f, 0.0f, 5.0f, 1.1f, 5.0f, -1.1f);
  quad(5.0f, -1.1f, 5.0f, 1.1f, -7.0f, 0.9f, -7.0f, -0.9f);
  // Swept main wings: long and slim so the silhouette still reads at 16 px.
  quad(2.0f, 1.0f, -2.0f, 7.5f, -3.8f, 7.5f, -1.0f, 1.0f);
  quad(2.0f, -1.0f, -1.0f, -1.0f, -3.8f, -7.5f, -2.0f, -7.5f);
  // Tailplane.
  quad(-5.0f, 0.8f, -7.2f, 3.6f, -8.4f, 3.6f, -7.0f, 0.8f);
  quad(-5.0f, -0.8f, -7.0f, -0.8f, -8.4f, -3.6f, -7.2f, -3.6f);
}

}  // namespace ui::radar
