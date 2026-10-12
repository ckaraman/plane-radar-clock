#include "ui/map_overlay.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "services/map_store.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::mapoverlay {

namespace {

constexpr float kKmPerDeg = 111.0f;  // same flat projection as the radar itself
constexpr float kDegToRad = 0.01745329252f;

// Muted on purpose: the map must stay behind the grid and the aircraft, but the
// sea is a clearly lighter tone than the land (which stays the radar background).
constexpr uint8_t kWaterR = 11, kWaterG = 34, kWaterB = 70;
constexpr uint8_t kCoastR = 70, kCoastG = 140, kCoastB = 220;
constexpr uint8_t kRoadR = 170, kRoadG = 92, kRoadB = 20;  // muted orange

constexpr int kMaskStride = (radar::kSize + 7) / 8;
constexpr size_t kMaskBytes = static_cast<size_t>(kMaskStride) * radar::kSize;
constexpr int kMaxCross = 96;  // crossings kept per scanline

// 1 bit per pixel: sea. Rebuilt only when the zoom, center or map changes.
uint8_t* s_mask = nullptr;
bool s_mask_any = false;
uint32_t s_key_gen = 0;
float s_key_scale = 0.0f;
float s_key_ox = 0.0f;
float s_key_oy = 0.0f;

// The GC9A01 here is a BGR panel (see radar_display.cpp): swap R/B so the colors read as written.
uint16_t panelColor(lgfx::LGFXBase& gfx, uint8_t r, uint8_t g, uint8_t b) {
  return config::kDisplayRgbOrder ? gfx.color565(b, g, r) : gfx.color565(r, g, b);
}

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

// Even-odd scanline fill of all type-2 rings into the 1-bit mask.
void rebuildMask(const uint8_t* blob, size_t size, uint16_t count, float ox, float oy,
                 float scale) {
  s_key_gen = services::map::generation();
  s_key_scale = scale;
  s_key_ox = ox;
  s_key_oy = oy;
  s_mask_any = false;

  // Index the rings first so every scanline can walk them without re-parsing.
  struct Ring {
    const uint8_t* pts;
    uint16_t n;
  };
  constexpr int kMaxRings = 64;
  Ring rings[kMaxRings];
  int ring_count = 0;
  size_t i = services::map::kHeaderBytes;
  for (uint16_t k = 0; k < count; ++k) {
    if (i + 3 > size) {
      break;
    }
    const uint8_t type = blob[i];
    const uint16_t n = rdU16(blob + i + 1);
    i += 3;
    if (i + static_cast<size_t>(n) * 4 > size) {
      break;
    }
    if (type == 2 && n >= 3 && ring_count < kMaxRings) {
      rings[ring_count++] = {blob + i, n};
    }
    i += static_cast<size_t>(n) * 4;
  }
  if (ring_count == 0) {
    return;
  }
  if (s_mask == nullptr) {
    s_mask = static_cast<uint8_t*>(malloc(kMaskBytes));
    if (s_mask == nullptr) {
      return;
    }
  }
  memset(s_mask, 0, kMaskBytes);

  for (int y = 0; y < radar::kSize; ++y) {
    const float yc = static_cast<float>(y) + 0.5f;
    float cross[kMaxCross];
    int nc = 0;
    for (int r = 0; r < ring_count; ++r) {
      const Ring& ring = rings[r];
      float px = ox + static_cast<float>(rdI16(ring.pts + (ring.n - 1) * 4)) * scale;
      float py = oy - static_cast<float>(rdI16(ring.pts + (ring.n - 1) * 4 + 2)) * scale;
      for (uint16_t p = 0; p < ring.n; ++p) {
        const float x = ox + static_cast<float>(rdI16(ring.pts + p * 4)) * scale;
        const float yy = oy - static_cast<float>(rdI16(ring.pts + p * 4 + 2)) * scale;
        if ((py <= yc && yy > yc) || (yy <= yc && py > yc)) {
          const float cx = px + (yc - py) * (x - px) / (yy - py);
          if (nc < kMaxCross) {
            int j = nc++;
            while (j > 0 && cross[j - 1] > cx) {  // keep sorted
              cross[j] = cross[j - 1];
              --j;
            }
            cross[j] = cx;
          }
        }
        px = x;
        py = yy;
      }
    }
    uint8_t* row = s_mask + static_cast<size_t>(y) * kMaskStride;
    for (int c = 0; c + 1 < nc; c += 2) {
      int x0 = static_cast<int>(ceilf(cross[c] - 0.5f));
      int x1 = static_cast<int>(ceilf(cross[c + 1] - 0.5f)) - 1;  // inclusive
      if (x0 < 0) x0 = 0;
      if (x1 > radar::kSize - 1) x1 = radar::kSize - 1;
      for (int x = x0; x <= x1; ++x) {
        row[x >> 3] |= static_cast<uint8_t>(1u << (x & 7));
      }
      if (x1 >= x0) {
        s_mask_any = true;
      }
    }
  }
}

void drawMask(lgfx::LGFXBase& gfx, uint16_t color) {
  if (s_mask == nullptr || !s_mask_any) {
    return;
  }
  for (int y = 0; y < radar::kSize; ++y) {
    const uint8_t* row = s_mask + static_cast<size_t>(y) * kMaskStride;
    int x = 0;
    while (x < radar::kSize) {
      if (row[x >> 3] == 0 && (x & 7) == 0) {
        x += 8;
        continue;
      }
      if (!(row[x >> 3] & (1u << (x & 7)))) {
        ++x;
        continue;
      }
      int x1 = x;
      while (x1 + 1 < radar::kSize && (row[(x1 + 1) >> 3] & (1u << ((x1 + 1) & 7)))) {
        ++x1;
      }
      gfx.drawFastHLine(x, y, x1 - x + 1, color);
      x = x1 + 1;
    }
  }
}

}  // namespace

void draw(lgfx::LGFXBase& gfx) {
  const uint8_t* blob = services::map::data();
  const size_t size = services::map::size();
  if (blob == nullptr || size < services::map::kHeaderBytes) {
    return;
  }

  if (radar::rangeCurrent().outer_km > radar::kMapMaxOuterKm) {
    return;  // widest range: no map, the stored data only covers up to 25 km
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

  if (blob[2] >= 2) {
    if (services::map::generation() != s_key_gen || scale != s_key_scale || ox != s_key_ox ||
        oy != s_key_oy) {
      rebuildMask(blob, size, count, ox, oy, scale);
    }
    drawMask(gfx, panelColor(gfx, kWaterR, kWaterG, kWaterB));
  }

  const uint16_t coast = panelColor(gfx, kCoastR, kCoastG, kCoastB);
  const uint16_t road = panelColor(gfx, kRoadR, kRoadG, kRoadB);
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
    if (type > 1) {  // sea rings are drawn through the mask above
      i += static_cast<size_t>(n) * 4;
      continue;
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
