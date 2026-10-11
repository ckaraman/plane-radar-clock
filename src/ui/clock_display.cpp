#include "ui/clock_display.h"

#include <Arduino.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <cstdio>
#include <ctime>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/clock_service.h"

namespace ui {

namespace {

// Black / white / mid-grey are symmetric in R,G,B, so the panel's BGR order
// and inversion settings cannot tint them.
constexpr uint16_t kBackground = 0x0000;
constexpr uint16_t kTimeColor = 0xFFFF;
constexpr uint16_t kDateColor = 0x7BEF;

constexpr int kCenterX = 120;
constexpr int kTimeY = 108;
constexpr int kDateY = 168;
// Opaque text only repaints the glyph cells, so pad to wipe the widest string.
constexpr int kTimePadding = 200;
constexpr int kDatePadding = 160;

int s_last_minute = -1;
int s_last_yday = -1;

// ASCII only: the bitmap font has no Turkish letters, so Ç/Ş/Ğ become C/S/G.
const char* const kWeekdaysEn[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* const kMonthsEn[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                 "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const char* const kWeekdaysTr[] = {"PAZ", "PZT", "SAL", "CAR", "PER", "CUM", "CMT"};
const char* const kMonthsTr[] = {"OCA", "SUB", "MAR", "NIS", "MAY", "HAZ",
                                 "TEM", "AGU", "EYL", "EKI", "KAS", "ARA"};

}  // namespace

void clockDisplayEnter() {
  s_last_minute = -1;
  s_last_yday = -1;
  tft.fillScreen(kBackground);
}

void clockDisplayTick() {
  tm t;
  if (!services::clock::now(&t)) {
    return;
  }
  if (t.tm_min == s_last_minute && t.tm_yday == s_last_yday) {
    return;
  }
  s_last_minute = t.tm_min;
  s_last_yday = t.tm_yday;

  char time_text[8];
  snprintf(time_text, sizeof(time_text), "%02d:%02d", t.tm_hour, t.tm_min);
  char date_text[20];
  const char* const* weekdays = config::kClockTurkishDate ? kWeekdaysTr : kWeekdaysEn;
  const char* const* months = config::kClockTurkishDate ? kMonthsTr : kMonthsEn;
  snprintf(date_text, sizeof(date_text), "%s %02d %s", weekdays[t.tm_wday % 7],
           t.tm_mday, months[t.tm_mon % 12]);

  tft.setTextDatum(textdatum_t::middle_center);

  // HH:MM in the built-in 7-segment font (digits and colon only).
  tft.setFont(&fonts::Font7);
  tft.setTextSize(1.3f);
  tft.setTextPadding(kTimePadding);
  tft.setTextColor(kTimeColor, kBackground);
  tft.drawString(time_text, kCenterX, kTimeY);

  // Date line in a bitmap font, via the helper that resets size and drops any
  // active smooth font on this instance.
  displayFontSetBitmap(tft, &fonts::FreeSansBold12pt7b);
  tft.setTextPadding(kDatePadding);
  tft.setTextColor(kDateColor, kBackground);
  tft.drawString(date_text, kCenterX, kDateY);

  // Don't leak padding/datum into the radar's own text drawing.
  tft.setTextPadding(0);
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui
