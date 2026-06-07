#pragma once

#include <Arduino.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/BaseSerialInterface.h>

#include "DataStore.h"

#ifndef FEATURE_GPS_TRACKER
#define FEATURE_GPS_TRACKER 0
#endif

#ifndef GPS_TRACKER_INTERVAL_DEFAULT
#define GPS_TRACKER_INTERVAL_DEFAULT 60
#endif

#ifndef GPS_TRACKER_HOP_LIMIT_DEFAULT
#define GPS_TRACKER_HOP_LIMIT_DEFAULT 3
#endif

#ifndef GPS_TRACKER_HISTORY_MAX
#define GPS_TRACKER_HISTORY_MAX 50
#endif

#ifndef GPS_TRACKER_FIX_TIMEOUT_DEFAULT
#define GPS_TRACKER_FIX_TIMEOUT_DEFAULT 120
#endif

#ifndef GPS_TRACKER_MIN_MOVEMENT_M_DEFAULT
#define GPS_TRACKER_MIN_MOVEMENT_M_DEFAULT 50
#endif

namespace gps_tracker {

constexpr uint32_t HISTORY_MAGIC = 0x47505452UL;  // GPTR
constexpr uint16_t HISTORY_VERSION = 1;
constexpr uint16_t TRACKER_ADV_MAGIC = 0x4700;

enum Flags : uint8_t {
  FLAG_FIX_VALID = 1 << 0,
  FLAG_MOVING = 1 << 1,
  FLAG_SHARED = 1 << 2,
};

#pragma pack(push, 1)
struct Record {
  uint32_t timestamp;
  int32_t latitude_e6;
  int32_t longitude_e6;
  int16_t altitude_m;
  uint8_t hdop_x10;
  uint8_t satellites;
  uint8_t battery_pct;
  uint8_t flags;
};

struct HistoryHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t capacity;
  uint16_t count;
  uint16_t head;
};
#pragma pack(pop)

static_assert(sizeof(Record) == 18, "gps_tracker::Record must remain packed");

class Store {
public:
  explicit Store(DataStore& data_store);

  void begin(uint16_t capacity);
  bool append(const Record& record);
  void clear();
  uint16_t count() const;
  uint16_t capacity() const;
  bool latest(Record& record) const;
  uint16_t copyRecent(uint16_t limit, Record out_records[], uint16_t out_capacity) const;

private:
  FILESYSTEM* historyFs() const;
  bool ensureFile(uint16_t desired_capacity) const;
  bool loadHeader(HistoryHeader& header) const;
  bool saveHeader(const HistoryHeader& header) const;
  bool writeEntry(uint16_t index, const Record& record) const;
  bool readEntry(uint16_t index, Record& record) const;

  DataStore* _data_store;
  mutable uint16_t _capacity;
};

bool isJsonCommand(const uint8_t frame[], size_t len);
bool extractBool(const char* json, const char* key, bool default_value);
uint32_t extractUInt(const char* json, const char* key, uint32_t default_value);
bool extractString(const char* json, const char* key, char* dest, size_t dest_size);
void escapeJsonString(const char* src, char* dest, size_t dest_size);
uint8_t batteryPctFromMv(uint16_t battery_mv);
float distanceMeters(int32_t lat1_e6, int32_t lon1_e6, int32_t lat2_e6, int32_t lon2_e6);
uint16_t encodeTrackerFeat2(uint8_t hop_limit);
bool decodeTrackerHopLimit(const uint8_t app_data[], size_t app_data_len, uint8_t& hop_limit);
void writeJsonText(BaseSerialInterface& serial, const char* text);
void writeJsonRecord(BaseSerialInterface& serial, const Record& record, bool with_type, const char* type_name, bool moving_override);

}  // namespace gps_tracker
