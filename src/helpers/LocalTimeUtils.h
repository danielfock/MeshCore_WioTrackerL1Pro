#pragma once

#include <stdint.h>
#include <RTClib.h>

namespace mesh {
namespace localtime {

inline uint8_t daysInMonth(uint16_t year, uint8_t month) {
  switch (month) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      return 31;
    case 4:
    case 6:
    case 9:
    case 11:
      return 30;
    case 2:
      return ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) ? 29 : 28;
    default:
      return 30;
  }
}

inline uint8_t lastSundayOfMonth(uint16_t year, uint8_t month) {
  uint8_t last_day = daysInMonth(year, month);
  DateTime last_date(year, month, last_day, 0, 0, 0);
  return static_cast<uint8_t>(last_day - last_date.dayOfTheWeek());
}

inline bool isEuropeViennaDstUtc(uint32_t utc_seconds) {
  DateTime utc(utc_seconds);
  uint8_t month = utc.month();
  if (month < 3 || month > 10) return false;
  if (month > 3 && month < 10) return true;

  uint8_t switch_day = lastSundayOfMonth(utc.year(), month);
  if (month == 3) {
    if (utc.day() > switch_day) return true;
    if (utc.day() < switch_day) return false;
    return utc.hour() >= 1;
  }

  if (utc.day() < switch_day) return true;
  if (utc.day() > switch_day) return false;
  return utc.hour() < 1;
}

inline int32_t europeViennaOffsetSeconds(uint32_t utc_seconds) {
  return isEuropeViennaDstUtc(utc_seconds) ? 7200L : 3600L;
}

inline DateTime europeViennaDateTime(uint32_t utc_seconds) {
  return DateTime(static_cast<uint32_t>(utc_seconds + europeViennaOffsetSeconds(utc_seconds)));
}

inline const char* europeViennaZoneLabel(uint32_t utc_seconds) {
  return isEuropeViennaDstUtc(utc_seconds) ? "CEST" : "CET";
}

}  // namespace localtime
}  // namespace mesh
