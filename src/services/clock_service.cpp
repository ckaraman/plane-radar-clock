#include "services/clock_service.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::clock {

namespace {

// Own NVS namespace: the other modules open theirs with separate handles.
constexpr char kPrefsNamespace[] = "clock";
constexpr char kKeyTz[] = "tz";
constexpr char kKeyOn[] = "on";

// Anything before this is "clock never set" (the RTC boots at 1970).
constexpr time_t kMinValidEpoch = 1700000000;  // Nov 2023

constexpr size_t kTzMaxLen = 48;

char s_tz[kTzMaxLen] = {};
bool s_enabled = true;
bool s_started = false;

void applyTimezone() {
  setenv("TZ", s_tz, 1);
  tzset();
}

bool checkboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  // WiFiManager submits the checkbox's value= attribute ("T") when ticked.
  return (value[0] == 'T' || value[0] == 't') && value[1] == '\0';
}

void useDefaults() {
  strlcpy(s_tz, config::kDefaultTimezone, sizeof(s_tz));
  s_enabled = true;
}

}  // namespace

void init() {
  useDefaults();
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    String tz = prefs.getString(kKeyTz, config::kDefaultTimezone);
    if (tz.length() > 0 && tz.length() < kTzMaxLen) {
      strlcpy(s_tz, tz.c_str(), sizeof(s_tz));
    }
    s_enabled = prefs.getBool(kKeyOn, true);
    prefs.end();
  }
  applyTimezone();
}

void startSync() {
  if (s_started) {
    return;
  }
  s_started = true;
  // SNTP keeps retrying in the background, so this is fine to call as soon as
  // WiFi is up; the time simply appears once the first reply arrives.
  configTzTime(s_tz, config::kNtpServer1, config::kNtpServer2);
  Serial.printf("Clock: NTP started, TZ=%s\n", s_tz);
}

bool now(tm* out) {
  const time_t t = time(nullptr);
  if (t < kMinValidEpoch) {
    return false;
  }
  localtime_r(&t, out);
  return true;
}

bool synced() { return time(nullptr) >= kMinValidEpoch; }

bool enabled() { return s_enabled; }

const char* timezone() { return s_tz; }

void saveFromPortal(const char* tz, const char* enabled_checkbox) {
  if (tz != nullptr && tz[0] != '\0' && strlen(tz) < kTzMaxLen) {
    strlcpy(s_tz, tz, sizeof(s_tz));
  } else {
    Serial.println("Clock: invalid timezone in portal — keeping previous");
  }
  s_enabled = checkboxChecked(enabled_checkbox);

  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putString(kKeyTz, s_tz);
    prefs.putBool(kKeyOn, s_enabled);
    prefs.end();
  }
  applyTimezone();  // takes effect immediately, no reboot needed
  Serial.printf("Clock: %s, TZ=%s\n", s_enabled ? "on" : "off", s_tz);
}

void clear() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
  useDefaults();
  applyTimezone();
}

}  // namespace services::clock
