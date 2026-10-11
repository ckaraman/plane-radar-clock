#pragma once

#include <ctime>

namespace services::clock {

/** Load saved timezone / on-off flag from NVS (defaults from config.h). */
void init();

/** Start NTP + apply the timezone. Safe to call repeatedly; runs once. */
void startSync();

/** Local time. Returns false until NTP has set the clock. */
bool now(tm* out);
bool synced();

/** Show the clock when no aircraft are around. */
bool enabled();
const char* timezone();

/** Portal values: POSIX TZ string and checkbox ("T" = on, empty = off). */
void saveFromPortal(const char* tz, const char* enabled_checkbox);

/** Restore defaults (called with the WiFi/location reset). */
void clear();

}  // namespace services::clock
