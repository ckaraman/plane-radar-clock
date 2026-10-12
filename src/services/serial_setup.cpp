#include "services/serial_setup.h"

#include <Arduino.h>
#include <WiFi.h>

#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "services/clock_service.h"
#include "services/map_store.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_range.h"

namespace services::serial_setup {

namespace {

constexpr size_t kMaxLine = 700;
constexpr size_t kMaxReply = 2048;
constexpr size_t kTzMaxLen = 47;
constexpr unsigned long kJoinTimeoutMs = 20000;
constexpr unsigned long kNotFoundGiveUpMs = 8000;
constexpr size_t kMaxScanResults = 20;

char s_line[kMaxLine];
size_t s_len = 0;
bool s_overflow = false;
char s_reply[kMaxReply];

// One write() per line keeps a reply from being split by the log output of
// the ADS-B task, which prints from the other core.
void sendReply(const char* tag, const JsonDocument& doc) {
  int n = snprintf(s_reply, sizeof(s_reply), "PR:%s ", tag);
  if (n < 0) {
    return;
  }
  const size_t room = sizeof(s_reply) - static_cast<size_t>(n) - 2;
  const size_t body = serializeJson(doc, s_reply + n, room);
  size_t total = static_cast<size_t>(n) + body;
  s_reply[total++] = '\n';
  Serial.write(reinterpret_cast<const uint8_t*>(s_reply), total);
}

void sendError(const char* code) {
  JsonDocument doc;
  doc["msg"] = code;
  sendReply("ERR", doc);
}

void sendOk(bool rebooting, const char* ip = nullptr) {
  JsonDocument doc;
  doc["reboot"] = rebooting;
  if (ip != nullptr) {
    doc["ip"] = ip;
  }
  sendReply("OK", doc);
}

void sendHello() {
  JsonDocument doc;
  doc["fw"] = config::kFirmwareVersion;
  doc["board"] = "esp32c3";

  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char mac_text[13];
  snprintf(mac_text, sizeof(mac_text), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  doc["mac"] = mac_text;

  const bool connected = WiFi.status() == WL_CONNECTED;
  doc["wifi"] = connected;
  char ssid[33] = {};
  char ip[16] = {};
  if (connected) {
    strlcpy(ssid, WiFi.SSID().c_str(), sizeof(ssid));
    strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
  }
  doc["ssid"] = ssid;
  doc["ip"] = ip;

  doc["lat"] = services::location::lat();
  doc["lon"] = services::location::lon();
  doc["tz"] = services::clock::timezone();
  doc["clock"] = services::clock::enabled();
  doc["miles"] = ui::radar::useMiles();
  doc["runways"] = ui::radar::showRunways();
  doc["map"] = static_cast<uint32_t>(services::map::size());
  sendReply("HELLO", doc);
}

void ensureStaEnabled() {
  const wifi_mode_t mode = WiFi.getMode();
  if ((mode & WIFI_MODE_STA) == 0) {
    WiFi.mode(static_cast<wifi_mode_t>(mode | WIFI_MODE_STA));
  }
}

void sendScan() {
  ensureStaEnabled();
  const int found = WiFi.scanNetworks(false, false);
  JsonDocument doc;
  JsonArray nets = doc["nets"].to<JsonArray>();

  // Strongest first; skip duplicates (same SSID on several access points).
  bool used[64] = {};
  const int limit = found < 64 ? found : 64;
  size_t listed = 0;
  while (listed < kMaxScanResults) {
    int best = -1;
    for (int i = 0; i < limit; ++i) {
      if (!used[i] && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) {
        best = i;
      }
    }
    if (best < 0) {
      break;
    }
    used[best] = true;
    const String name = WiFi.SSID(best);
    if (name.length() == 0) {
      continue;
    }
    bool duplicate = false;
    for (JsonObject n : nets) {
      if (name == n["s"].as<const char*>()) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    JsonObject n = nets.add<JsonObject>();
    n["s"] = name;
    n["r"] = WiFi.RSSI(best);
    n["o"] = WiFi.encryptionType(best) == WIFI_AUTH_OPEN;
    ++listed;
  }
  WiFi.scanDelete();
  sendReply("SCAN", doc);
}

struct JoinResult {
  bool ok;
  const char* reason;
};

// Tries the network and, on success, leaves its credentials saved in flash.
// On failure the previous credentials are put back, so a typo cannot lock a
// working device out of its old network.
JoinResult tryJoin(const char* ssid, const char* pass) {
  ensureStaEnabled();

  wifi_config_t previous = {};
  const bool have_previous =
      esp_wifi_get_config(WIFI_IF_STA, &previous) == ESP_OK &&
      previous.sta.ssid[0] != '\0';

  WiFi.persistent(true);
  esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(ssid, pass);

  const unsigned long started = millis();
  wl_status_t status = WL_IDLE_STATUS;
  while (millis() - started < kJoinTimeoutMs) {
    status = WiFi.status();
    if (status == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      WiFi.setAutoReconnect(true);
      return {true, nullptr};
    }
    if (status == WL_CONNECT_FAILED) {
      break;
    }
    if (status == WL_NO_SSID_AVAIL && millis() - started > kNotFoundGiveUpMs) {
      break;
    }
    delay(100);
  }

  WiFi.disconnect(false, false);
  if (have_previous) {
    esp_wifi_set_config(WIFI_IF_STA, &previous);
  } else {
    WiFi.disconnect(false, true);  // nothing to go back to: forget the attempt
  }

  if (status == WL_NO_SSID_AVAIL) {
    return {false, "ssid_not_found"};
  }
  if (status == WL_CONNECT_FAILED) {
    return {false, "auth_failed"};
  }
  return {false, "timeout"};
}

bool optionalBool(const JsonDocument& doc, const char* key, bool fallback) {
  return doc[key].is<bool>() ? doc[key].as<bool>() : fallback;
}

void handleSet(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    sendError("bad_json");
    return;
  }

  // Validate everything first so a bad field never leaves a half-applied set.
  const bool has_lat = doc["lat"].is<double>();
  const bool has_lon = doc["lon"].is<double>();
  if (has_lat != has_lon) {
    sendError("bad_location");
    return;
  }
  double lat = 0.0;
  double lon = 0.0;
  if (has_lat) {
    lat = doc["lat"].as<double>();
    lon = doc["lon"].as<double>();
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
      sendError("bad_location");
      return;
    }
  }

  const char* tz = doc["tz"].is<const char*>() ? doc["tz"].as<const char*>() : nullptr;
  if (tz != nullptr && (tz[0] == '\0' || strlen(tz) > kTzMaxLen)) {
    sendError("bad_timezone");
    return;
  }

  const char* ssid =
      doc["ssid"].is<const char*>() ? doc["ssid"].as<const char*>() : "";
  const char* pass =
      doc["pass"].is<const char*>() ? doc["pass"].as<const char*>() : "";
  if (strlen(ssid) > 32 || strlen(pass) > 63) {
    sendError("bad_wifi");
    return;
  }
  if (ssid[0] != '\0' && pass[0] != '\0' && strlen(pass) < 8) {
    sendError("bad_wifi_password");  // WPA passwords are 8..63 characters
    return;
  }

  // Settings that do not depend on the network.
  if (has_lat) {
    char lat_text[24];
    char lon_text[24];
    snprintf(lat_text, sizeof(lat_text), "%.6f", lat);
    snprintf(lon_text, sizeof(lon_text), "%.6f", lon);
    services::location::saveFromStrings(lat_text, lon_text);
  }
  if (tz != nullptr || doc["clock"].is<bool>()) {
    char tz_copy[kTzMaxLen + 1];
    strlcpy(tz_copy, tz != nullptr ? tz : services::clock::timezone(),
            sizeof(tz_copy));
    const bool clock_on = optionalBool(doc, "clock", services::clock::enabled());
    services::clock::saveFromPortal(tz_copy, clock_on ? "T" : "");
  }
  if (doc["miles"].is<bool>()) {
    ui::radar::saveMilesFromPortal(doc["miles"].as<bool>() ? "T" : "");
  }
  if (doc["runways"].is<bool>()) {
    ui::radar::saveRunwaysFromPortal(doc["runways"].as<bool>() ? "T" : "");
  }

  if (ssid[0] == '\0') {
    sendOk(false);
    return;
  }

  const JoinResult joined = tryJoin(ssid, pass);
  if (!joined.ok) {
    sendError(joined.reason);
    return;
  }

  char ip[16];
  strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
  sendOk(true, ip);
  Serial.flush();
  delay(700);
  esp_restart();  // clean start on the saved network
}

int b64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Decodes standard base64 (with or without '=' padding) into out; -1 on bad input.
int b64Decode(const char* in, uint8_t* out, size_t out_cap) {
  size_t n = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (; *in != '\0' && *in != '='; ++in) {
    const int v = b64Value(*in);
    if (v < 0) {
      return -1;
    }
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= out_cap) {
        return -1;
      }
      out[n++] = static_cast<uint8_t>((acc >> bits) & 0xFF);
    }
  }
  return static_cast<int>(n);
}

// PR:MAPBEGIN {"len":n,"crc":c}   PR:MAP <seq> <base64>   PR:MAPEND   PR:MAPCLEAR
// Every chunk is acknowledged (PR:MAPACK) so the page never outruns the chip.
void handleMapBegin(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok ||
      !doc["len"].is<uint32_t>() || !doc["crc"].is<uint32_t>()) {
    sendError("bad_json");
    return;
  }
  if (!services::map::beginStaging(doc["len"].as<uint32_t>(),
                                   doc["crc"].as<uint32_t>())) {
    sendError("map_too_big");
    return;
  }
  JsonDocument ack;
  ack["seq"] = 0;
  sendReply("MAPACK", ack);
}

void handleMapChunk(char* args) {
  char* data = strchr(args, ' ');
  if (data == nullptr) {
    sendError("bad_map_chunk");
    return;
  }
  *data++ = '\0';
  const long seq = atol(args);
  uint8_t bytes[480];
  const int n = b64Decode(data, bytes, sizeof(bytes));
  if (n <= 0 || !services::map::stageChunk(bytes, static_cast<size_t>(n))) {
    services::map::abortStaging();
    sendError("bad_map_chunk");
    return;
  }
  JsonDocument ack;
  ack["seq"] = seq;
  sendReply("MAPACK", ack);
}

void handleMapEnd() {
  if (!services::map::commit()) {
    sendError("bad_map");
    return;
  }
  JsonDocument doc;
  doc["map"] = static_cast<uint32_t>(services::map::size());
  sendReply("OK", doc);
}

void handleMapClear() {
  services::map::clear();
  sendOk(false);
}

void handleReset() {
  sendOk(true);
  Serial.flush();
  delay(200);
  wifiResetCredentialsAndReboot();
}

void handleLine(char* line) {
  if (strncmp(line, "PR:", 3) != 0) {
    return;  // ordinary log text
  }
  char* command = line + 3;
  char* args = strchr(command, ' ');
  if (args != nullptr) {
    *args++ = '\0';
  } else {
    args = command + strlen(command);  // empty string
  }

  if (strcmp(command, "HELLO") == 0) {
    sendHello();
  } else if (strcmp(command, "SCAN") == 0) {
    sendScan();
  } else if (strcmp(command, "SET") == 0) {
    handleSet(args);
  } else if (strcmp(command, "MAPBEGIN") == 0) {
    handleMapBegin(args);
  } else if (strcmp(command, "MAP") == 0) {
    handleMapChunk(args);
  } else if (strcmp(command, "MAPEND") == 0) {
    handleMapEnd();
  } else if (strcmp(command, "MAPCLEAR") == 0) {
    handleMapClear();
  } else if (strcmp(command, "RESET") == 0) {
    handleReset();
  } else {
    sendError("unknown_command");
  }
}

}  // namespace

void poll() {
  int budget = 512;  // bytes per call; keeps the loop responsive
  while (budget-- > 0 && Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      if (!s_overflow && s_len > 0) {
        s_line[s_len] = '\0';
        handleLine(s_line);
      }
      s_len = 0;
      s_overflow = false;
      continue;
    }
    if (s_len < kMaxLine - 1) {
      s_line[s_len++] = static_cast<char>(c);
    } else {
      s_overflow = true;
    }
  }
}

}  // namespace services::serial_setup
