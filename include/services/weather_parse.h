#pragma once

#include <ArduinoJson.h>

#include <cmath>
#include <cstdint>

namespace services::weather {

enum class Condition : uint8_t {
  Clear,
  PartlyCloudy,
  Cloudy,
  Fog,
  Drizzle,
  Rain,
  Showers,
  Snow,
  Thunder,
};

struct Slot {
  bool valid = false;
  int16_t temp_c = 0;  // whole degrees Celsius
  Condition cond = Condition::Clear;
  bool day = true;
};

struct Forecast {
  Slot now;
  Slot later;  // about two hours ahead
};

/** WMO weather interpretation code (as returned by Open-Meteo) -> icon class. */
inline Condition conditionFromWmo(int code) {
  switch (code) {
    case 0:
    case 1:
      return Condition::Clear;
    case 2:
      return Condition::PartlyCloudy;
    case 3:
      return Condition::Cloudy;
    case 45:
    case 48:
      return Condition::Fog;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return Condition::Drizzle;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
      return Condition::Rain;
    case 80:
    case 81:
    case 82:
      return Condition::Showers;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return Condition::Snow;
    case 95:
    case 96:
    case 99:
      return Condition::Thunder;
    default:
      return Condition::Cloudy;
  }
}

/** Short ASCII label (the bitmap font has no Turkish letters). */
inline const char* conditionLabel(Condition c) {
  switch (c) {
    case Condition::Clear: return "ACIK";
    case Condition::PartlyCloudy: return "P.BULUTLU";
    case Condition::Cloudy: return "BULUTLU";
    case Condition::Fog: return "SISLI";
    case Condition::Drizzle: return "CISELEME";
    case Condition::Rain: return "YAGMURLU";
    case Condition::Showers: return "SAGANAK";
    case Condition::Snow: return "KARLI";
    case Condition::Thunder: return "FIRTINA";
  }
  return "";
}

/**
 * Open-Meteo request this parser expects:
 *   current=temperature_2m,weather_code,is_day
 *   hourly=temperature_2m,weather_code,is_day   (forecast_hours=6)
 *   timeformat=unixtime  (all timestamps are UTC epoch seconds)
 * Picks the hourly slot closest to "current time + 2 h".
 */
inline bool parseForecast(const JsonDocument& doc, Forecast* out) {
  JsonVariantConst cur = doc["current"];
  if (!cur["temperature_2m"].is<float>() || !cur["weather_code"].is<int>() ||
      !cur["time"].is<long>()) {
    return false;
  }
  Forecast f;
  f.now.valid = true;
  f.now.temp_c = static_cast<int16_t>(lroundf(cur["temperature_2m"].as<float>()));
  f.now.cond = conditionFromWmo(cur["weather_code"].as<int>());
  f.now.day = cur["is_day"] | 1;

  const long cur_time = cur["time"].as<long>();
  const long target = cur_time + 2 * 3600;
  JsonArrayConst times = doc["hourly"]["time"];
  JsonArrayConst temps = doc["hourly"]["temperature_2m"];
  JsonArrayConst codes = doc["hourly"]["weather_code"];
  JsonArrayConst days = doc["hourly"]["is_day"];
  const size_t n = times.size();
  if (n > 0 && temps.size() == n && codes.size() == n) {
    long best_diff = 1L << 30;
    size_t best = n;
    for (size_t i = 0; i < n; ++i) {
      if (temps[i].isNull() || codes[i].isNull()) {
        continue;
      }
      long diff = times[i].as<long>() - target;
      if (diff < 0) {
        diff = -diff;
      }
      if (diff < best_diff) {
        best_diff = diff;
        best = i;
      }
    }
    // Hourly data: the nearest slot is at most 30 min off.
    if (best < n && best_diff <= 1800) {
      f.later.valid = true;
      f.later.temp_c = static_cast<int16_t>(lroundf(temps[best].as<float>()));
      f.later.cond = conditionFromWmo(codes[best].as<int>());
      f.later.day = days.size() == n ? (days[best] | 1) != 0 : true;
    }
  }
  *out = f;
  return true;
}

}  // namespace services::weather
