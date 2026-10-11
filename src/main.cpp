/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/clock_service.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/clock_display.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_redraw_ms = 0;

// Which screen is up while connected. The clock replaces the radar while the
// sky has been empty for config::kClockIdleDelayMs, and hands back the moment
// an aircraft shows up (or the BOOT button is tapped).
enum class Screen { Radar, Clock };
Screen g_screen = Screen::Radar;
unsigned long g_last_aircraft_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  services::clock::startSync();  // no-op after the first call
  g_screen = Screen::Radar;
  g_last_aircraft_ms = millis();
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

// Runs at the redraw cadence: decide radar vs clock, then draw one frame.
void updateScreen() {
  const unsigned long now = millis();
  const size_t aircraft = services::adsb::aircraftCount();
  // Until the first fetch finishes the list is empty only because we haven't
  // asked yet, so that must not count as "sky is empty".
  const bool have_data = services::adsb::lastUpdateMs() != 0;
  if (aircraft > 0 || !have_data) {
    g_last_aircraft_ms = now;
  }

  const bool idle = services::clock::enabled() && services::clock::synced() &&
                    now - g_last_aircraft_ms >= config::kClockIdleDelayMs;

  if (idle) {
    if (g_screen != Screen::Clock) {
      g_screen = Screen::Clock;
      ui::clockDisplayEnter();
    }
    ui::clockDisplayTick();
    return;
  }

  if (g_screen == Screen::Clock) {
    g_screen = Screen::Radar;
    ui::radarDisplayDraw();  // full redraw over the clock
    return;
  }
  ui::radarDisplayRefreshAircraft();
}

void onRangeTap() {
  if (g_screen == Screen::Clock) {
    // First tap only wakes the radar; the range stays as it was. The idle timer
    // restarts, so the clock returns after another kClockIdleDelayMs of silence.
    g_last_aircraft_ms = millis();
    return;
  }
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onRangeTap();
  }
}

// ADS-B fetch runs on its own task: the HTTPS request blocks for ~1-2 s, and
// keeping it off the main loop lets the radar keep redrawing (dead-reckoned) at
// 4 Hz throughout. The task publishes into the shared aircraft buffer under a
// lock; the render loop reads a snapshot.
void adsbFetchTask(void*) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      services::adsb::fetchUpdate(services::location::lat(),
                                  services::location::lon(),
                                  ui::radar::fetchRadiusKm());
    }
    vTaskDelay(pdMS_TO_TICKS(config::kAdsbFetchIntervalMs));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  services::clock::init();  // before WiFi setup: the portal prefills from it
  ui::radar::rangeInit();
  services::adsb::init();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }

  // Start the background ADS-B fetch (pinned to core 0, away from the render
  // loop on core 1). It checks Wi-Fi state each cycle.
  xTaskCreatePinnedToCore(adsbFetchTask, "adsb", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_redraw_ms >= config::kRadarRedrawIntervalMs) {
      // Redraw at 4 Hz with dead-reckoned positions; the ADS-B fetch runs on
      // its own task (adsbFetchTask). When idle this shows the clock instead.
      g_last_redraw_ms = millis();
      updateScreen();
    }
  }

  delay(10);
}
