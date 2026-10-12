#pragma once

#include <cstddef>
#include <cstdint>

namespace services::map {

/**
 * Map layer (sea fill, coast lines and main roads) shown behind the radar.
 *
 * The setup page builds the data from OpenStreetMap and sends it over USB; the
 * device keeps it in the unused "spiffs" flash partition (raw, no file system)
 * and in RAM while running. Blob format (little endian):
 *   'P' 'M' version(1 or 2) flags(0)
 *   int32 center_lat_e7, int32 center_lon_e7, uint16 polyline_count
 *   per polyline: uint8 type (0 coast, 1 road, 2 water ring), uint16 n, n * (int16 x, int16 y)
 *   Type 2 rings are closed implicitly and filled even-odd (version 2).
 *   x/y are east/north offsets from the center in units of 10 m.
 */
constexpr size_t kMaxBytes = 32000;
/** Newest blob version this firmware understands (reported as "mapv" in HELLO). */
constexpr uint8_t kBlobVersion = 2;
constexpr size_t kHeaderBytes = 14;

/** Load the stored map (if any) into RAM. Call once at boot. */
void init();

/** Active map blob, or nullptr when no map is stored. */
const uint8_t* data();
size_t size();
/** Changes whenever the active map is replaced or removed (cache invalidation). */
uint32_t generation();

/** Staged upload from the USB page: begin, feed chunks in order, commit. */
bool beginStaging(size_t length, uint32_t crc32);
bool stageChunk(const uint8_t* bytes, size_t n);
/** Verifies length, checksum and structure, writes flash, activates. */
bool commit();
void abortStaging();

/** Remove the stored map (flash and RAM). */
void clear();

uint32_t crc32(const uint8_t* bytes, size_t n);

}  // namespace services::map
