#include "services/map_store.h"

#include <Arduino.h>
#include <esp_partition.h>

#include <cstdlib>
#include <cstring>

namespace services::map {

namespace {

constexpr uint32_t kMagic = 0x504D5250;  // "PRMP"
constexpr size_t kSector = 4096;

struct FlashHeader {
  uint32_t magic;
  uint32_t length;
  uint32_t crc;
};

uint8_t* s_active = nullptr;
size_t s_active_len = 0;

uint8_t* s_stage = nullptr;
size_t s_stage_len = 0;
size_t s_stage_got = 0;
uint32_t s_stage_crc = 0;

const esp_partition_t* partition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                  ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
}

uint16_t rd16(const uint8_t* p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}

// The blob must be exactly consumed by its polylines; anything else is rejected
// so a damaged upload can never make the radar read outside the buffer.
bool validBlob(const uint8_t* b, size_t n) {
  if (n < kHeaderBytes || n > kMaxBytes || b[0] != 'P' || b[1] != 'M' || b[2] != 1) {
    return false;
  }
  const uint16_t count = rd16(b + 12);
  size_t i = kHeaderBytes;
  for (uint16_t k = 0; k < count; ++k) {
    if (i + 3 > n) {
      return false;
    }
    const uint16_t pts = rd16(b + i + 1);
    i += 3 + static_cast<size_t>(pts) * 4;
    if (i > n) {
      return false;
    }
  }
  return i == n;
}

void freeStage() {
  free(s_stage);
  s_stage = nullptr;
  s_stage_len = s_stage_got = 0;
}

}  // namespace

uint32_t crc32(const uint8_t* bytes, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) {
    crc ^= bytes[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

void init() {
  const esp_partition_t* part = partition();
  if (part == nullptr) {
    return;
  }
  FlashHeader h;
  if (esp_partition_read(part, 0, &h, sizeof(h)) != ESP_OK || h.magic != kMagic ||
      h.length < kHeaderBytes || h.length > kMaxBytes) {
    return;
  }
  uint8_t* buf = static_cast<uint8_t*>(malloc(h.length));
  if (buf == nullptr) {
    Serial.println("map: no RAM for stored map");
    return;
  }
  if (esp_partition_read(part, sizeof(h), buf, h.length) != ESP_OK ||
      crc32(buf, h.length) != h.crc || !validBlob(buf, h.length)) {
    free(buf);
    Serial.println("map: stored map is damaged, ignoring it");
    return;
  }
  s_active = buf;
  s_active_len = h.length;
  Serial.printf("map: loaded %u bytes\n", static_cast<unsigned>(h.length));
}

const uint8_t* data() { return s_active; }
size_t size() { return s_active_len; }

bool beginStaging(size_t length, uint32_t crc) {
  freeStage();
  if (length < kHeaderBytes || length > kMaxBytes) {
    return false;
  }
  s_stage = static_cast<uint8_t*>(malloc(length));
  if (s_stage == nullptr) {
    return false;
  }
  s_stage_len = length;
  s_stage_got = 0;
  s_stage_crc = crc;
  return true;
}

bool stageChunk(const uint8_t* bytes, size_t n) {
  if (s_stage == nullptr || s_stage_got + n > s_stage_len) {
    return false;
  }
  memcpy(s_stage + s_stage_got, bytes, n);
  s_stage_got += n;
  return true;
}

bool commit() {
  if (s_stage == nullptr || s_stage_got != s_stage_len ||
      crc32(s_stage, s_stage_len) != s_stage_crc || !validBlob(s_stage, s_stage_len)) {
    freeStage();
    return false;
  }
  const esp_partition_t* part = partition();
  if (part == nullptr) {
    freeStage();
    return false;
  }
  const FlashHeader h = {kMagic, static_cast<uint32_t>(s_stage_len), s_stage_crc};
  const size_t total = sizeof(h) + s_stage_len;
  const size_t erase_len = ((total + kSector - 1) / kSector) * kSector;
  if (esp_partition_erase_range(part, 0, erase_len) != ESP_OK ||
      esp_partition_write(part, 0, &h, sizeof(h)) != ESP_OK ||
      esp_partition_write(part, sizeof(h), s_stage, s_stage_len) != ESP_OK) {
    freeStage();
    return false;
  }
  free(s_active);
  s_active = s_stage;
  s_active_len = s_stage_len;
  s_stage = nullptr;
  s_stage_len = s_stage_got = 0;
  return true;
}

void abortStaging() { freeStage(); }

void clear() {
  freeStage();
  free(s_active);
  s_active = nullptr;
  s_active_len = 0;
  const esp_partition_t* part = partition();
  if (part != nullptr) {
    esp_partition_erase_range(part, 0, kSector);  // header sector only
  }
}

}  // namespace services::map
