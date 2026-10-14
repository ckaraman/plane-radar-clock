#include "ui/weather_display.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <cstdio>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/weather_service.h"
#include "ui/weather_icons.h"

namespace ui {

namespace {

using services::weather::Forecast;
using services::weather::Slot;

unsigned long s_drawn_fetch_ms = 0;
bool s_drawn_any = false;

// The GC9A01 here is a BGR panel: swap R/B so the colours read as written.
uint16_t col(uint8_t r, uint8_t g, uint8_t b) {
  return config::kDisplayRgbOrder ? tft.color565(b, g, r) : tft.color565(r, g, b);
}

constexpr int kCenterX = 120;

/** Big temperature ("18" + degree ring + "C") centred on cx. */
void drawTemperature(int cx, int cy, int temp_c, const lgfx::GFXfont* big,
                     const lgfx::GFXfont* small, int ring_r, uint16_t color, uint16_t bg) {
  char num[8];
  snprintf(num, sizeof(num), "%d", temp_c);
  displayFontSetBitmap(tft, big);
  const int num_w = tft.textWidth(num);
  const int ring_w = ring_r * 2 + 4;
  displayFontSetBitmap(tft, small);
  const int unit_w = tft.textWidth("C");
  const int total = num_w + ring_w + unit_w;
  int x = cx - total / 2;

  tft.setTextDatum(textdatum_t::middle_left);
  tft.setTextColor(color, bg);
  displayFontSetBitmap(tft, big);
  tft.drawString(num, x, cy);
  x += num_w;

  const int ring_cy = cy - tft.fontHeight() / 3;
  tft.drawCircle(x + ring_r + 2, ring_cy, ring_r, color);
  tft.drawCircle(x + ring_r + 2, ring_cy, ring_r - 1, color);
  x += ring_w;

  displayFontSetBitmap(tft, small);
  tft.drawString("C", x, cy + 2);
}

void drawLabel(const char* text, int y, const lgfx::GFXfont* font, uint16_t color,
               uint16_t bg) {
  displayFontSetBitmap(tft, font);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(color, bg);
  tft.drawString(text, kCenterX, y);
}

void drawSlot(const Slot& s, int icon_cx, int icon_cy, int icon_r, int temp_cx,
              const lgfx::GFXfont* big, const lgfx::GFXfont* small, int ring_r,
              int label_y, const weather::IconPalette<uint16_t>& pal, uint16_t white,
              uint16_t soft, uint16_t bg) {
  if (!s.valid) {
    drawLabel("--", icon_cy, big, soft, bg);
    return;
  }
  weather::drawWeatherIcon(tft, icon_cx, icon_cy, icon_r, s.cond, s.day, pal);
  drawTemperature(temp_cx, icon_cy, s.temp_c, big, small, ring_r, white, bg);
  drawLabel(services::weather::conditionLabel(s.cond), label_y, &fonts::FreeSansBold9pt7b,
            soft, bg);
}

void drawScreen(const Forecast& f) {
  const uint16_t bg = 0x0000;
  const uint16_t white = 0xFFFF;
  const uint16_t soft = col(190, 200, 215);
  const uint16_t dim = col(130, 140, 158);
  const uint16_t line = col(50, 58, 78);
  weather::IconPalette<uint16_t> pal{bg,
                                     col(255, 200, 40),
                                     col(225, 225, 200),
                                     col(215, 225, 235),
                                     col(130, 145, 165),
                                     col(70, 150, 255),
                                     col(240, 245, 255),
                                     col(255, 210, 0),
                                     col(170, 180, 195)};

  tft.fillScreen(bg);
  drawLabel("SIMDI", 30, &fonts::FreeSansBold9pt7b, dim, bg);
  drawSlot(f.now, 66, 80, 32, 160, &fonts::FreeSansBold24pt7b, &fonts::FreeSansBold12pt7b, 4,
           118, pal, white, soft, bg);

  tft.drawFastHLine(52, 134, 136, line);

  drawLabel("2 SAAT SONRA", 150, &fonts::FreeSansBold9pt7b, dim, bg);
  drawSlot(f.later, 74, 184, 22, 156, &fonts::FreeSansBold18pt7b, &fonts::FreeSansBold9pt7b,
           3, 216, pal, white, soft, bg);

  tft.setTextPadding(0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void weatherDisplayEnter() {
  Forecast f;
  unsigned long fetched = 0;
  if (!services::weather::snapshot(&f, &fetched)) {
    f = Forecast();
  }
  drawScreen(f);
  s_drawn_fetch_ms = fetched;
  s_drawn_any = true;
}

void weatherDisplayTick() {
  Forecast f;
  unsigned long fetched = 0;
  if (!services::weather::snapshot(&f, &fetched)) {
    return;
  }
  if (s_drawn_any && fetched == s_drawn_fetch_ms) {
    return;
  }
  drawScreen(f);
  s_drawn_fetch_ms = fetched;
  s_drawn_any = true;
}

}  // namespace ui
