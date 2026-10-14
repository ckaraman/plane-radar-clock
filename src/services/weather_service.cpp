#include "services/weather_service.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cmath>
#include <cstdio>

#include "config.h"

namespace services::weather {

namespace {

constexpr char kPrefsNamespace[] = "weather";
constexpr char kKeyOn[] = "on";

constexpr int kConnectTimeoutMs = 6000;
constexpr unsigned long kRequestTimeoutMs = 8000;

bool s_enabled = true;
Forecast s_forecast;
bool s_have = false;
unsigned long s_fetched_ms = 0;
unsigned long s_last_try_ms = 0;
bool s_tried = false;
double s_lat = 1000.0;  // location of the data we hold (impossible until first fetch)
double s_lon = 1000.0;
SemaphoreHandle_t s_mutex = nullptr;

void lock() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
  if (s_mutex != nullptr) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
  }
}

void unlock() {
  if (s_mutex != nullptr) {
    xSemaphoreGive(s_mutex);
  }
}

bool checkboxChecked(const char* value) {
  return value != nullptr && (value[0] == 'T' || value[0] == 't') && value[1] == '\0';
}

bool fetchNow(double lat, double lon) {
  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,is_day"
           "&hourly=temperature_2m,weather_code,is_day&forecast_hours=6"
           "&timeformat=unixtime&timezone=GMT",
           lat, lon);

  JsonDocument filter;
  filter["current"]["time"] = true;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["hourly"]["time"] = true;
  filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["weather_code"] = true;
  filter["hourly"]["is_day"] = true;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);  // plain body, no chunked framing
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kRequestTimeoutMs);
  if (!http.begin(client, url)) {
    Serial.println("weather: http.begin failed");
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("weather: HTTP %d\n", code);
    http.end();
    return false;
  }
  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("weather: JSON error: %s\n", err.c_str());
    return false;
  }
  Forecast f;
  if (!parseForecast(doc, &f)) {
    Serial.println("weather: unexpected response");
    return false;
  }
  lock();
  s_forecast = f;
  s_have = true;
  s_fetched_ms = millis();
  s_lat = lat;
  s_lon = lon;
  unlock();
  Serial.printf("weather: now %d C (%s), +2h %s%d C\n", f.now.temp_c,
                conditionLabel(f.now.cond), f.later.valid ? "" : "n/a ",
                f.later.valid ? f.later.temp_c : 0);
  return true;
}

}  // namespace

void init() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    s_enabled = prefs.getBool(kKeyOn, true);
    prefs.end();
  }
}

bool enabled() { return s_enabled; }

void saveFromPortal(const char* enabled_checkbox) {
  s_enabled = checkboxChecked(enabled_checkbox);
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putBool(kKeyOn, s_enabled);
    prefs.end();
  }
  Serial.printf("Weather: %s\n", s_enabled ? "on" : "off");
}

void clear() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
  s_enabled = true;
}

bool snapshot(Forecast* out, unsigned long* fetched_ms) {
  lock();
  const bool have = s_have;
  if (have) {
    if (out != nullptr) {
      *out = s_forecast;
    }
    if (fetched_ms != nullptr) {
      *fetched_ms = s_fetched_ms;
    }
  }
  unlock();
  return have;
}

void fetchIfDue(double lat, double lon) {
  if (!s_enabled || WiFi.status() != WL_CONNECTED) {
    return;
  }
  const unsigned long now = millis();
  lock();
  const bool moved = std::fabs(lat - s_lat) > 0.01 || std::fabs(lon - s_lon) > 0.01;
  const bool stale = !s_have || now - s_fetched_ms >= config::kWeatherRefreshMs;
  const bool retry_ok = !s_tried || now - s_last_try_ms >= config::kWeatherRetryMs;
  unlock();
  if (!(moved || stale) || !retry_ok) {
    return;
  }
  s_tried = true;
  s_last_try_ms = now;
  fetchNow(lat, lon);
}

}  // namespace services::weather
