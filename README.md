# Plane Radar

<img width="800" height="450" alt="plane-radar" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

**3D printed case (STL + assembly):** [MakerWorld](https://makerworld.com/en/models/2872376-esp32-plane-radar-live-ads-b-on-a-round-display#profileId-3207083) · **Firmware:** [Releases](https://github.com/MatixYo/ESP32-Plane-Radar/releases)

Firmware for an **ESP32-C3 Super Mini** and a **1.28″ round GC9A01** display (240×240). Shows a circular **ADS-B radar** around your configured location, with **WiFiManager** for first-time setup.

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`PlaneRadar-Setup`**
2. **Radar** — live aircraft from [adsb.fi](https://opendata.adsb.fi/) on a sonar-style grid

After Wi‑Fi is saved, the device reconnects automatically; the radar runs in the main loop with periodic ADS-B updates (~5 s).

## Controls (BOOT, GPIO 9, active LOW)

| Action | Effect |
|--------|--------|
| **Short tap** | Cycle range preset (5 → 10 → 15 → 17 → 25 km; the map layer is not drawn on the widest 25 km range, outer edge ~33 km); saved to flash |
| **Hold 3 s** | Clear Wi‑Fi, location, and units; reboot into setup portal |

During setup you can also hold BOOT at power-on to force a credential reset (same as the long press).

## Wi‑Fi setup portal

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`PlaneRadar-Setup`**
2. Open **`http://plane-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically
3. Set home Wi‑Fi, then save

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://plane-radar.local`** or **`http://<device-ip>`** (e.g. from your router or serial log at boot)
2. Change Wi‑Fi, location, units, or runway overlay; save

The same portal runs on the setup AP and on the device’s LAN IP while connected to Wi‑Fi. mDNS hostname is `plane-radar` → **plane-radar.local** (`kPortalHostname` in `config.h`). Some clients resolve `.local` slowly; use the IP if needed.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** | Radar center and ADS-B query position (defaults in `config.h` until set) |
| **Display distances in miles** | Ring scale label in **mi** instead of **km** (e.g. `6mi` vs `10km`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |
| **Time zone** | POSIX TZ string for the idle clock; the drop-down above it fills in common zones (default in `config.h`, `kDefaultTimezone`) |
| **Show clock when no aircraft** | Turns the idle clock on/off |

### Idle clock

When no aircraft have been in range for `kClockIdleDelayMs` (15 s), the radar is replaced by a large **HH:MM** clock with the date. As soon as an aircraft appears the radar returns. A **short BOOT tap** while the clock is up wakes the radar (it does not change the range). Time comes from NTP (`pool.ntp.org`, `time.google.com`) once Wi‑Fi is up; until the first sync the radar stays on screen. Date names are Turkish by default (`kClockTurkishDate` in `config.h`).

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

## Setup and update page (USB, from a computer)

`web/` is a small static site (published to GitHub Pages by `.github/workflows/build.yml`) that lets an end user set up a device that was flashed in advance:

1. Plug the device into a computer, open the page in **Chrome or Edge** and press *Cihaza bağlan* (connect).
2. Pick the Wi-Fi network (scan list), enter the password, choose the location (city search, "use my location" or lat/lon) and time zone, press *Cihaza kaydet*.
3. The device tests the Wi-Fi, stores everything and restarts. A wrong password is reported on the page and the previous Wi-Fi is kept.

The page talks to the firmware over the USB serial port with a line protocol (`PR:HELLO`, `PR:SCAN`, `PR:SET {json}`, `PR:RESET`; see `include/services/serial_setup.h`). Ordinary log lines are ignored, so Serial Monitor still works.

**Updating:** the same page has an *update* button (ESP Web Tools). It writes bootloader, partition table and application (`0x0`, `0x8000`, `0x10000`) but not the settings area, so Wi-Fi/location/time zone stay on the device. In the installer dialog leave *Erase device* unticked. ESP Web Tools erases the whole flash by default for firmware that does not speak Improv Serial, which is why the manifests set `new_install_prompt_erase: true` (it shows the checkbox). *Full install* (under "Gelişmiş") writes the merged image at `0x0` and **erases the settings**; use it for blank devices or if a release changes the partition table (`partitions/`).

To publish: in the repository, **Settings → Pages → Source: GitHub Actions**. Every push to `main` builds the firmware and redeploys the site. Bump `kFirmwareVersion` in `config.h` for each release, the page compares it with the version on the device.

To flash devices before shipping, write `firmware-merged.bin` at `0x0` (the *Full install* button does this, or `esptool`). A device without saved Wi-Fi shows the yellow setup screen and can still be set up through the old Wi-Fi portal (`PlaneRadar-Setup`, no password).

## Map layer (sea, coast lines and main roads)

The setup page can draw the coast line and main roads (motorway and trunk only) behind the radar. When you press *Cihaza kaydet* with the widest range you will use selected (5/10/15/17 km, default 15; *Haritasız* = no download), the page downloads the data around the chosen location out to 1.13 x that range's outer ring (so the round screen is filled to its edge) from OpenStreetMap (Overpass API), builds sea polygons from the coastline (OSM keeps water on the right of the coastline), simplifies it to at most ~3000 line points plus ~2500 sea points (~15-30 KB) and sends it over USB (`PR:MAPBEGIN` / `PR:MAP` / `PR:MAPEND`, every chunk acknowledged). The device keeps it in the unused `spiffs` partition (raw, no file system) and draws it under the grid (`src/ui/map_overlay.cpp`). The sea is filled with a lighter tone through a cached 1-bit mask (rebuilt only on zoom/map change). Colors are `kWater*` / `kCoast*` / `kRoad*` in that file (the panel is BGR, so they are swapped through `config::kDisplayRgbOrder`; roads are orange). Blob version 2 adds type-2 sea rings (even-odd fill); `HELLO` reports `mapv`, and the page only sends sea rings to firmware with `mapv` >= 2. Map data © OpenStreetMap contributors (ODbL). The map is removed by *Ayarları sıfırla* and by the BOOT long press.

## Radar display

### Grid

- Dark blue background, subdued green rings and crosshairs
- White **N / S / E / W** at the bezel; range label on the **east** spoke (ring 3 = ¾ of outer radius)
- White center dot

Layout and colors: `include/ui/radar_theme.h`.

### Range presets

| Ring 3 label | Outer radius (aircraft scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 17 km / 11 mi | ~22.7 km |
| 25 km / 16 mi | ~33.3 km (no map layer) |

Preset and miles/km choice persist across reboot (`planeradar` NVS namespace).

### Runways

- Major airports from OurAirports (`large_airport`); all open runway strips in range (helipads excluded)
- Teal runway lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- Update the embedded list: `python3 scripts/build_large_airports.py`

### Aircraft

- **Inside the outer ring** — red heading triangle, magenta speed vector (clipped at the ring), callsign / type / altitude tags
- **Outside the ring** (still within ADS-B fetch) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring)
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**

As range decreases (or aircraft approach), targets move inward; beyond-ring dots become full symbols when they cross the outer ring.

### ADS-B

- Source: `https://opendata.adsb.fi/api/v3/`
- Fetch radius: `ui::radar::fetchRadiusKm()` — scales with the active preset to roughly the screen edge (so rim dots have data)
- Poll interval: `kAdsbFetchIntervalMs` (5 s) in `config.h`
- Ground aircraft hidden by default (`kAdsbShowGroundAircraft`)

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (until portal overrides) |
| ADS-B | `kAdsbFetchIntervalMs`, `kAdsbShowGroundAircraft` |

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

## Project layout

```
include/
  config.h
  hardware/
    lgfx_config.hpp
    display.h
    display_font.h
  data/
    large_airports.h
  ui/
    radar_theme.h
    radar_range.h
    radar_display.h
    runway_overlay.h
    status_screens.h
  services/
    wifi_setup.h
    radar_location.h
    adsb_client.h
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
scripts/
  build_large_airports.py
src/
  main.cpp
  data/
    large_airports_data.cpp
  hardware/
  ui/
  services/
```

## Wiring (GC9A01 ↔ ESP32-C3 Super Mini)

| Display | ESP32-C3 |
|---------|----------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **10** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BOOT (user) | GPIO **9** |

## Build

```bash
pio run -t upload
pio device monitor
```

- PlatformIO env: **`supermini`**
- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini` for the Super Mini

### Web-flashable release image

Single `.bin` for [esptool-js](https://espressif.github.io/esptool-js/) and similar tools (ESP32-C3, 4 MB, flash at **0x0**):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh
```

Writes `release/plane-radar-merged.bin`. Skip rebuild if firmware is already built:

```bash
./scripts/merge-firmware.sh --no-build
```

Or via PlatformIO only (output: `.pio/build/supermini/firmware-merged.bin`):

```bash
pio run -e supermini
pio run -t merge -e supermini
```

Put the board in download mode (hold **BOOT**, tap **RESET**), then flash with Chrome/Edge over USB.

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push / PR to `main` | Artifact `plane-radar-supermini` (merged + split `.bin` files, ~90 days) |
| [Release](.github/workflows/release.yml) | Git tag `v*` (e.g. `v1.0.0`) | GitHub Release asset `plane-radar-v1.0.0.bin` + `.sha256` |

To ship a version users can download:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The release workflow builds firmware in CI and attaches the merged image to the release. Download from **Releases** on GitHub, then flash at **0x0** (ESP32-C3, 4 MB).

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
