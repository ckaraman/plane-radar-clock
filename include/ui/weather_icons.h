#pragma once

#include <cmath>

#include "services/weather_parse.h"

namespace ui::weather {

template <typename Color>
struct IconPalette {
  Color background;
  Color sun;
  Color moon;
  Color cloud;
  Color cloud_dark;
  Color rain;
  Color snow;
  Color bolt;
  Color fog;
};

namespace detail {

template <typename Gfx, typename Color>
void thickLine(Gfx& g, float x0, float y0, float x1, float y1, float w, const Color& c) {
  // A line as a quad: two triangles around the centre line.
  const float dx = x1 - x0, dy = y1 - y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.01f) {
    return;
  }
  const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
  const int ax = lroundf(x0 + nx), ay = lroundf(y0 + ny);
  const int bx = lroundf(x0 - nx), by = lroundf(y0 - ny);
  const int cx = lroundf(x1 - nx), cy = lroundf(y1 - ny);
  const int dx2 = lroundf(x1 + nx), dy2 = lroundf(y1 + ny);
  g.fillTriangle(ax, ay, bx, by, cx, cy, c);
  g.fillTriangle(ax, ay, cx, cy, dx2, dy2, c);
}

template <typename Gfx, typename Color>
void sun(Gfx& g, float cx, float cy, float r, const Color& c) {
  g.fillCircle(lroundf(cx), lroundf(cy), lroundf(r * 0.45f), c);
  const float w = r * 0.12f > 1.5f ? r * 0.12f : 1.5f;
  for (int i = 0; i < 8; ++i) {
    const float a = i * 0.7853982f;
    const float ux = std::cos(a), uy = std::sin(a);
    thickLine(g, cx + ux * r * 0.64f, cy + uy * r * 0.64f, cx + ux * r * 0.95f,
              cy + uy * r * 0.95f, w, c);
  }
}

template <typename Gfx, typename Color>
void moon(Gfx& g, float cx, float cy, float r, const Color& c, const Color& bg) {
  g.fillCircle(lroundf(cx), lroundf(cy), lroundf(r * 0.62f), c);
  g.fillCircle(lroundf(cx + r * 0.32f), lroundf(cy - r * 0.2f), lroundf(r * 0.52f), bg);
}

template <typename Gfx, typename Color>
void cloud(Gfx& g, float cx, float cy, float r, const Color& c) {
  g.fillCircle(lroundf(cx - r * 0.55f), lroundf(cy + r * 0.2f), lroundf(r * 0.28f), c);
  g.fillCircle(lroundf(cx - r * 0.1f), lroundf(cy - r * 0.1f), lroundf(r * 0.45f), c);
  g.fillCircle(lroundf(cx + r * 0.45f), lroundf(cy + r * 0.12f), lroundf(r * 0.33f), c);
  const int x0 = lroundf(cx - r * 0.55f);
  const int x1 = lroundf(cx + r * 0.45f);
  const int y0 = lroundf(cy + r * 0.2f);
  const int y1 = lroundf(cy + r * 0.47f);
  g.fillRect(x0, y0, x1 - x0, y1 - y0 + 1, c);
}

template <typename Gfx, typename Color>
void slantedDrops(Gfx& g, float cx, float cy, float r, float len, int n, float w,
                  const Color& c) {
  for (int i = 0; i < n; ++i) {
    const float x = cx - r * 0.4f + (n > 1 ? i * (r * 0.8f / (n - 1)) : 0.0f);
    thickLine(g, x, cy, x - len * 0.35f, cy + len, w, c);
  }
}

}  // namespace detail

/**
 * Weather symbol centred on (cx, cy) inside a square of roughly 2*r px.
 * `Gfx` needs fillCircle, fillRect and fillTriangle; `Color` is the panel colour type.
 */
template <typename Gfx, typename Color>
void drawWeatherIcon(Gfx& g, int cx, int cy, int r, services::weather::Condition cond,
                     bool day, const IconPalette<Color>& p) {
  using services::weather::Condition;
  const float R = static_cast<float>(r);
  const float x = static_cast<float>(cx), y = static_cast<float>(cy);
  const float stroke = R * 0.1f > 1.5f ? R * 0.1f : 1.5f;

  auto sunOrMoon = [&](float ox, float oy, float scale) {
    if (day) {
      detail::sun(g, x + ox * R, y + oy * R, R * scale, p.sun);
    } else {
      detail::moon(g, x + ox * R, y + oy * R, R * scale, p.moon, p.background);
    }
  };

  switch (cond) {
    case Condition::Clear:
      sunOrMoon(0.0f, 0.0f, 1.0f);
      break;
    case Condition::PartlyCloudy:
      sunOrMoon(0.32f, -0.3f, 0.78f);
      detail::cloud(g, x - R * 0.12f, y + R * 0.22f, R * 0.88f, p.cloud);
      break;
    case Condition::Cloudy:
      detail::cloud(g, x + R * 0.28f, y - R * 0.2f, R * 0.7f, p.cloud_dark);
      detail::cloud(g, x - R * 0.1f, y + R * 0.18f, R * 0.92f, p.cloud);
      break;
    case Condition::Fog:
      detail::cloud(g, x, y - R * 0.3f, R * 0.9f, p.cloud_dark);
      for (int i = 0; i < 3; ++i) {
        const float ly = y + R * (0.3f + 0.25f * i);
        const float half = R * (0.8f - 0.12f * (i % 2));
        detail::thickLine(g, x - half, ly, x + half, ly, stroke, p.fog);
      }
      break;
    case Condition::Drizzle:
      detail::cloud(g, x, y - R * 0.25f, R * 0.95f, p.cloud_dark);
      for (int i = 0; i < 4; ++i) {
        const float dx = x - R * 0.5f + i * R * 0.33f;
        const float dy = y + R * (0.52f + 0.14f * (i % 2));
        g.fillCircle(lroundf(dx), lroundf(dy), lroundf(stroke * 0.8f), p.rain);
      }
      break;
    case Condition::Rain:
      detail::cloud(g, x, y - R * 0.25f, R * 0.95f, p.cloud_dark);
      detail::slantedDrops(g, x + R * 0.1f, y + R * 0.4f, R * 0.95f, R * 0.5f, 3, stroke,
                           p.rain);
      break;
    case Condition::Showers:
      sunOrMoon(0.5f, -0.5f, 0.55f);
      detail::cloud(g, x - R * 0.12f, y - R * 0.12f, R * 0.88f, p.cloud_dark);
      detail::slantedDrops(g, x + R * 0.05f, y + R * 0.45f, R * 0.95f, R * 0.5f, 3, stroke,
                           p.rain);
      break;
    case Condition::Snow:
      detail::cloud(g, x, y - R * 0.25f, R * 0.95f, p.cloud_dark);
      for (int i = 0; i < 3; ++i) {
        g.fillCircle(lroundf(x - R * 0.45f + i * R * 0.45f), lroundf(y + R * 0.52f),
                     lroundf(stroke * 0.9f), p.snow);
      }
      for (int i = 0; i < 2; ++i) {
        g.fillCircle(lroundf(x - R * 0.22f + i * R * 0.45f), lroundf(y + R * 0.82f),
                     lroundf(stroke * 0.9f), p.snow);
      }
      break;
    case Condition::Thunder: {
      detail::cloud(g, x, y - R * 0.28f, R * 0.95f, p.cloud_dark);
      const int bx = lroundf(x), by = lroundf(y + R * 0.2f);
      const int u = lroundf(R * 0.5f) > 4 ? lroundf(R * 0.5f) : 4;
      g.fillTriangle(bx + u / 3, by, bx - u / 2, by + u, bx, by + u, p.bolt);
      g.fillTriangle(bx, by + u, bx + u / 2, by + u, bx - u / 4, by + 2 * u, p.bolt);
      g.fillTriangle(bx - u / 2, by + u, bx + u / 2, by + u, bx + u / 3, by, p.bolt);
      break;
    }
  }
}

}  // namespace ui::weather
