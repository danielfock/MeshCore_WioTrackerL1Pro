#include "GpsTracker.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace gps_tracker {

namespace {

const char* kHistoryFilePath = "/gps_tracker_history";

const char* skipJsonWhitespace(const char* value) {
  while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
    value++;
  }
  return value;
}

bool readExact(File& file, void* dest, size_t len) {
  return file.read((uint8_t*)dest, len) == len;
}

File openReadFs(FILESYSTEM* fs, const char* path) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(path, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(path, "r");
#else
  return fs->open(path, "r", false);
#endif
}

File openWriteFs(FILESYSTEM* fs, const char* path, bool recreate) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (recreate) {
    fs->remove(path);
  }
  return fs->open(path, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  if (recreate) {
    fs->remove(path);
  }
  return fs->open(path, "w");
#else
  return fs->open(path, "w", recreate);
#endif
}

double toRadians(double degrees) {
  return degrees * 3.14159265358979323846 / 180.0;
}

bool matchJsonKey(const char* json, const char* key, const char*& value_start) {
  char pattern[40];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char* start = strstr(json, pattern);
  if (!start) return false;
  start += strlen(pattern);
  start = skipJsonWhitespace(start);
  if (*start != ':') return false;
  start = skipJsonWhitespace(start + 1);
  value_start = start;
  return true;
}

}  // namespace

Store::Store(DataStore& data_store) : _data_store(&data_store), _capacity(GPS_TRACKER_HISTORY_MAX) {}

FILESYSTEM* Store::historyFs() const {
  FILESYSTEM* fs = _data_store->getSecondaryFS();
  if (!fs) fs = _data_store->getPrimaryFS();
  return fs;
}

void Store::begin(uint16_t capacity) {
  if (capacity == 0) {
    capacity = GPS_TRACKER_HISTORY_MAX;
  }
  _capacity = capacity;
  ensureFile(_capacity);
}

bool Store::ensureFile(uint16_t desired_capacity) const {
  FILESYSTEM* fs = historyFs();
  if (!fs) return false;

  HistoryHeader header;
  if (loadHeader(header) && header.magic == HISTORY_MAGIC && header.version == HISTORY_VERSION &&
      header.capacity == desired_capacity) {
    return true;
  }

  File file = openWriteFs(fs, kHistoryFilePath, true);
  if (!file) return false;

  HistoryHeader fresh = {
    HISTORY_MAGIC,
    HISTORY_VERSION,
    desired_capacity,
    0,
    0,
  };
  bool success = file.write((const uint8_t*)&fresh, sizeof(fresh)) == sizeof(fresh);
  if (success) {
    Record empty{};
    for (uint16_t i = 0; i < desired_capacity; ++i) {
      if (file.write((const uint8_t*)&empty, sizeof(empty)) != sizeof(empty)) {
        success = false;
        break;
      }
    }
  }
  file.close();
  return success;
}

bool Store::loadHeader(HistoryHeader& header) const {
  FILESYSTEM* fs = historyFs();
  if (!fs || !fs->exists(kHistoryFilePath)) return false;

  File file = openReadFs(fs, kHistoryFilePath);
  if (!file) return false;
  bool success = readExact(file, &header, sizeof(header));
  file.close();
  return success;
}

bool Store::saveHeader(const HistoryHeader& header) const {
  FILESYSTEM* fs = historyFs();
  if (!fs) return false;
  File file = openWriteFs(fs, kHistoryFilePath, false);
  if (!file) return false;
  file.seek(0);
  bool success = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header);
  file.close();
  return success;
}

bool Store::writeEntry(uint16_t index, const Record& record) const {
  FILESYSTEM* fs = historyFs();
  if (!fs) return false;
  File file = openWriteFs(fs, kHistoryFilePath, false);
  if (!file) return false;
  size_t offset = sizeof(HistoryHeader) + (static_cast<size_t>(index) * sizeof(Record));
  file.seek(offset);
  bool success = file.write((const uint8_t*)&record, sizeof(record)) == sizeof(record);
  file.close();
  return success;
}

bool Store::readEntry(uint16_t index, Record& record) const {
  FILESYSTEM* fs = historyFs();
  if (!fs) return false;
  File file = openReadFs(fs, kHistoryFilePath);
  if (!file) return false;
  size_t offset = sizeof(HistoryHeader) + (static_cast<size_t>(index) * sizeof(Record));
  file.seek(offset);
  bool success = readExact(file, &record, sizeof(record));
  file.close();
  return success;
}

bool Store::append(const Record& record) {
  HistoryHeader header;
  if (!loadHeader(header)) {
    if (!ensureFile(_capacity) || !loadHeader(header)) {
      return false;
    }
  }

  if (header.capacity == 0) return false;
  if (!writeEntry(header.head, record)) return false;

  header.head = (header.head + 1) % header.capacity;
  if (header.count < header.capacity) {
    header.count++;
  }
  return saveHeader(header);
}

void Store::clear() {
  FILESYSTEM* fs = historyFs();
  if (!fs) return;
  File file = openWriteFs(fs, kHistoryFilePath, true);
  if (!file) return;

  HistoryHeader fresh = {
    HISTORY_MAGIC,
    HISTORY_VERSION,
    _capacity,
    0,
    0,
  };
  if (file.write((const uint8_t*)&fresh, sizeof(fresh)) == sizeof(fresh)) {
    Record empty{};
    for (uint16_t i = 0; i < _capacity; ++i) {
      if (file.write((const uint8_t*)&empty, sizeof(empty)) != sizeof(empty)) {
        break;
      }
    }
  }
  file.close();
}

uint16_t Store::count() const {
  HistoryHeader header;
  if (!loadHeader(header)) return 0;
  return header.count;
}

uint16_t Store::capacity() const {
  HistoryHeader header;
  if (!loadHeader(header)) return _capacity;
  return header.capacity;
}

bool Store::latest(Record& record) const {
  HistoryHeader header;
  if (!loadHeader(header) || header.count == 0 || header.capacity == 0) return false;
  uint16_t index = header.head == 0 ? header.capacity - 1 : header.head - 1;
  return readEntry(index, record);
}

uint16_t Store::copyRecent(uint16_t limit, Record out_records[], uint16_t out_capacity) const {
  HistoryHeader header;
  if (!loadHeader(header) || header.count == 0 || out_capacity == 0) return 0;

  uint16_t max_items = header.count;
  if (limit > 0 && limit < max_items) max_items = limit;
  if (max_items > out_capacity) max_items = out_capacity;

  uint16_t copied = 0;
  int32_t index = header.head == 0 ? header.capacity - 1 : header.head - 1;
  while (copied < max_items) {
    if (!readEntry((uint16_t)index, out_records[copied])) {
      break;
    }
    copied++;
    index--;
    if (index < 0) {
      index = header.capacity - 1;
    }
  }
  return copied;
}

bool isJsonCommand(const uint8_t frame[], size_t len) {
  size_t start = 0;
  while (start < len && (frame[start] == '\r' || frame[start] == '\n' || frame[start] == ' ' || frame[start] == '\t')) {
    start++;
  }
  while (len > 0 && (frame[len - 1] == '\r' || frame[len - 1] == '\n' || frame[len - 1] == ' ')) {
    len--;
  }
  return len > start && frame[start] == '{';
}

bool extractBool(const char* json, const char* key, bool default_value) {
  const char* value = nullptr;
  if (!matchJsonKey(json, key, value)) return default_value;
  if (strncmp(value, "true", 4) == 0) return true;
  if (strncmp(value, "false", 5) == 0) return false;
  return default_value;
}

uint32_t extractUInt(const char* json, const char* key, uint32_t default_value) {
  const char* value = nullptr;
  if (!matchJsonKey(json, key, value)) return default_value;
  uint32_t out = 0;
  bool has_digits = false;
  while (*value >= '0' && *value <= '9') {
    has_digits = true;
    out = (out * 10U) + static_cast<uint32_t>(*value - '0');
    value++;
  }
  return has_digits ? out : default_value;
}

bool extractString(const char* json, const char* key, char* dest, size_t dest_size) {
  if (!dest || dest_size == 0) return false;
  dest[0] = 0;

  const char* value = nullptr;
  if (!matchJsonKey(json, key, value) || *value != '"') return false;
  value++;

  size_t out = 0;
  while (*value && *value != '"') {
    char ch = *value++;
    if (ch == '\\') {
      char escaped = *value++;
      switch (escaped) {
        case '"':
          ch = '"';
          break;
        case '\\':
          ch = '\\';
          break;
        case '/':
          ch = '/';
          break;
        case 'b':
          ch = '\b';
          break;
        case 'f':
          ch = '\f';
          break;
        case 'n':
          ch = '\n';
          break;
        case 'r':
          ch = '\r';
          break;
        case 't':
          ch = '\t';
          break;
        default:
          if (escaped == 0) {
            return false;
          }
          ch = escaped;
          break;
      }
    }

    if (out + 1 >= dest_size) {
      break;
    }
    dest[out++] = ch;
  }

  dest[out] = 0;
  return *value == '"';
}

void escapeJsonString(const char* src, char* dest, size_t dest_size) {
  if (!dest || dest_size == 0) return;
  if (!src) {
    dest[0] = 0;
    return;
  }

  size_t out = 0;
  while (*src && out + 1 < dest_size) {
    const char* replacement = nullptr;
    char ch = *src++;
    switch (ch) {
      case '"':
        replacement = "\\\"";
        break;
      case '\\':
        replacement = "\\\\";
        break;
      case '\n':
        replacement = "\\n";
        break;
      case '\r':
        replacement = "\\r";
        break;
      case '\t':
        replacement = "\\t";
        break;
      default:
        break;
    }

    if (replacement) {
      size_t repl_len = strlen(replacement);
      if (out + repl_len >= dest_size) {
        break;
      }
      memcpy(dest + out, replacement, repl_len);
      out += repl_len;
    } else {
      dest[out++] = ch;
    }
  }
  dest[out] = 0;
}

uint8_t batteryPctFromMv(uint16_t battery_mv) {
#ifndef BATT_MIN_MILLIVOLTS
#define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
#define BATT_MAX_MILLIVOLTS 4200
#endif
  if (battery_mv <= BATT_MIN_MILLIVOLTS) return 0;
  if (battery_mv >= BATT_MAX_MILLIVOLTS) return 100;
  return static_cast<uint8_t>(((battery_mv - BATT_MIN_MILLIVOLTS) * 100) /
                              (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS));
}

float distanceMeters(int32_t lat1_e6, int32_t lon1_e6, int32_t lat2_e6, int32_t lon2_e6) {
  const double lat1 = toRadians(static_cast<double>(lat1_e6) / 1000000.0);
  const double lon1 = toRadians(static_cast<double>(lon1_e6) / 1000000.0);
  const double lat2 = toRadians(static_cast<double>(lat2_e6) / 1000000.0);
  const double lon2 = toRadians(static_cast<double>(lon2_e6) / 1000000.0);
  const double d_lat = lat2 - lat1;
  const double d_lon = lon2 - lon1;

  const double sin_lat = sin(d_lat / 2.0);
  const double sin_lon = sin(d_lon / 2.0);
  const double aa = sin_lat * sin_lat + cos(lat1) * cos(lat2) * sin_lon * sin_lon;
  const double c = 2.0 * atan2(sqrt(aa), sqrt(1.0 - aa));
  return static_cast<float>(6371000.0 * c);
}

uint16_t encodeTrackerFeat2(uint8_t hop_limit) {
  return TRACKER_ADV_MAGIC | (hop_limit & 0x0F);
}

bool decodeTrackerHopLimit(const uint8_t app_data[], size_t app_data_len, uint8_t& hop_limit) {
  if (app_data_len == 0) return false;
  AdvertDataParser parser(app_data, app_data_len);
  if (!parser.isValid()) return false;
  if (parser.getType() != ADV_TYPE_SENSOR) return false;
  const uint16_t feat2 = parser.getFeat2();
  if ((feat2 & 0xFF00) != TRACKER_ADV_MAGIC) return false;
  hop_limit = feat2 & 0x0F;
  return true;
}

void writeJsonText(BaseSerialInterface& serial, const char* text) {
  size_t len = strlen(text);
  size_t offset = 0;
  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > MAX_FRAME_SIZE) chunk = MAX_FRAME_SIZE;
    serial.writeFrame(reinterpret_cast<const uint8_t*>(text + offset), chunk);
    offset += chunk;
  }
}

void writeJsonRecord(BaseSerialInterface& serial, const Record& record, bool with_type, const char* type_name, bool moving_override) {
  char line[192];
  const bool moving = moving_override || ((record.flags & FLAG_MOVING) != 0);
  if (with_type) {
    snprintf(line, sizeof(line),
             "{\"type\":\"%s\",\"ts\":%lu,\"lat\":%.5f,\"lon\":%.5f,\"alt\":%d,\"hdop\":%.1f,\"sats\":%u,\"bat\":%u,\"moving\":%s}\n",
             type_name,
             static_cast<unsigned long>(record.timestamp),
             static_cast<double>(record.latitude_e6) / 1000000.0,
             static_cast<double>(record.longitude_e6) / 1000000.0,
             static_cast<int>(record.altitude_m),
             static_cast<double>(record.hdop_x10) / 10.0,
             static_cast<unsigned>(record.satellites),
             static_cast<unsigned>(record.battery_pct),
             moving ? "true" : "false");
  } else {
    snprintf(line, sizeof(line),
             "{\"ts\":%lu,\"lat\":%.5f,\"lon\":%.5f,\"alt\":%d,\"hdop\":%.1f,\"sats\":%u,\"bat\":%u}",
             static_cast<unsigned long>(record.timestamp),
             static_cast<double>(record.latitude_e6) / 1000000.0,
             static_cast<double>(record.longitude_e6) / 1000000.0,
             static_cast<int>(record.altitude_m),
             static_cast<double>(record.hdop_x10) / 10.0,
             static_cast<unsigned>(record.satellites),
             static_cast<unsigned>(record.battery_pct));
  }
  writeJsonText(serial, line);
}

}  // namespace gps_tracker
