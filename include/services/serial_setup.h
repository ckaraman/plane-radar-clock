#pragma once

namespace services::serial_setup {

/**
 * USB setup channel for the web setup page (docs: web/index.html).
 *
 * Line protocol over the USB serial port, one command per line:
 *   PR:HELLO              -> PR:HELLO {fw, mac, wifi, ssid, ip, lat, lon, tz, clock, miles, runways}
 *   PR:SCAN               -> PR:SCAN {"nets":[{"s":ssid,"r":rssi,"o":open}, ...]}
 *   PR:SET {json}         -> PR:OK {...} | PR:ERR {"msg":code}
 *                            json keys (all optional): ssid, pass, lat, lon, tz,
 *                            clock, miles, runways
 *   PR:MAPBEGIN {len,crc} -> PR:MAPACK {seq}   start a map upload (see map_store.h)
 *   PR:MAP <seq> <b64>    -> PR:MAPACK {seq}   one chunk of the map blob
 *   PR:MAPEND             -> PR:OK {map} | PR:ERR {"msg":"bad_map"}
 *   PR:MAPCLEAR           -> PR:OK
 *   (HELLO also reports "map" = stored bytes and "mapv" = newest supported blob version)
 *   PR:RESET              -> PR:OK, then wipes Wi-Fi/location/units/map and reboots
 * Anything not starting with "PR:" (normal log output) is ignored.
 *
 * poll() is cheap when idle; call it from every loop that can run for a while
 * (main loop, Wi-Fi connect wait, config portal wait).
 */
void poll();

}  // namespace services::serial_setup
