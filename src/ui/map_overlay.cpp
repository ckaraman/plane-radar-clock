#include "ui/map_overlay.h"

#include <cmath>
#include <cstring>

#include "services/map_store.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::mapoverlay {

namespace {

constexpr float kKmPerDeg = 111.0f;  // same flat projection as the radar itself
constexpr float kDegToRad = 0.01745329252f;

// Dim on purpose: the map must stay behind the grid and the aircraft.
constexpr uint8_t kCoastR = 30, kCoastG = 70, kCoastB = 120;
constexpr uint8_t kRoadR = 105, kRoadG = 96, kRoadB = 52;

int16_t rdI16(const uint8_t* p) {
  int16_t v;
  memcpy(&v, p, 2);
  return v;
}

uint16_t rdU16(const uint8_t* p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

int32_t rdI32(const uint8_t* p) {
  int32_t v;
  memcpy(&v, p, 4);
  return v;
}

}  // namespace

void draw(lgfx::LGFXBase& gfx) {
  const uint8_t* blob = services::map::data();
  const size_t size = services::map::size();
  if (blob == nullptr || size < services::map::kHeaderBytes) {
    return;
  }

  const float map_lat = static_cast<float>(rdI32(blob + 4)) * 1e-7f;
  const float map_lon = static_cast<float>(rdI32(blob + 8)) * 1e-7f;
  const uint16_t count = rdU16(blob + 12);

  const float radar_lat = static_cast<float>(services::location::lat());
  const float radar_lon = static_cast<float>(services::location::lon());

  // Where the map's own center lies relative to the radar center (they match
  // unless the location was changed without refreshing the map).
  const float off_x_km =
      (map_lon - radar_lon) * kKmPerDeg * cosf(radar_lat * kDegToRad);
  const float off_y_km = (map_lat - radar_lat) * kKmPerDeg;

  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) /
                          radar::rangeCurrent().outer_km;
  const float scale = px_per_km * 0.01f;  // stored units are 10 m
  const float ox = radar::kCenterX + off_x_km * px_per_km;
  const float oy = radar::kCenterY - off_y_km * px_per_km;

  const uint16_t coast = gfx.color565(kCoastR, kCoastG, kCoastB);
  const uint16_t road = gfx.color565(kRoadR, kRoadG, kRoadB);
  constexpr float kLo = -8.0f;
  constexpr float kHi = static_cast<float>(radar::kSize) + 8.0f;

  size_t i = services::map::kHeaderBytes;
  for (uint16_t k = 0; k < count; ++k) {
    if (i + 3 > size) {
      return;
    }
    const uint8_t type = blob[i];
    const uint16_t n = rdU16(blob + i + 1);
    i += 3;
    if (i + static_cast<size_t>(n) * 4 > size) {
      return;
    }
    const uint16_t color = type == 0 ? coast : road;

    float px = 0.0f;
    float py = 0.0f;
    for (uint16_t p = 0; p < n; ++p) {
      const uint8_t* q = blob + i + static_cast<size_t>(p) * 4;
      const float x = ox + static_cast<float>(rdI16(q)) * scale;
      const float y = oy - static_cast<float>(rdI16(q + 2)) * scale;
      if (p > 0) {
        const bool off_screen = (x < kLo && px < kLo) || (x > kHi && px > kHi) ||
                                (y < kLo && py < kLo) || (y > kHi && py > kHi);
        if (!off_screen) {
          gfx.drawLine(static_cast<int32_t>(lroundf(px)), static_cast<int32_t>(lroundf(py)),
                       static_cast<int32_t>(lroundf(x)), static_cast<int32_t>(lroundf(y)),
                       color);
        }
      }
      px = x;
      py = y;
    }
    i += static_cast<size_t>(n) * 4;
  }
}

}  // namespace ui::mapoverlay
