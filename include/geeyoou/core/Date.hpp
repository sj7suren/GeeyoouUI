#pragma once
//
// Minimal proleptic-Gregorian date arithmetic.
//
// Written out rather than pulled from <chrono>'s calendar types because those
// need C++20 library support that MSVC, libstdc++ and libc++ shipped at very
// different times, and an HMI library should not force a toolchain upgrade to
// draw a month grid.  Everything here is integer arithmetic with no allocation.
//
#include <cstdint>
#include <cstdio>
#include <string>

namespace geeyoou {

struct Date {
  int year = 0;
  int month = 0;  // 1..12
  int day = 0;    // 1..31

  constexpr bool valid() const {
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
  }
  constexpr bool operator==(const Date& o) const {
    return year == o.year && month == o.month && day == o.day;
  }
  constexpr bool operator!=(const Date& o) const { return !(*this == o); }
  constexpr bool operator<(const Date& o) const {
    if (year != o.year) return year < o.year;
    if (month != o.month) return month < o.month;
    return day < o.day;
  }
  constexpr bool operator<=(const Date& o) const { return *this < o || *this == o; }
  constexpr bool operator>(const Date& o) const { return o < *this; }
  constexpr bool operator>=(const Date& o) const { return o <= *this; }
};

constexpr bool isLeapYear(int y) {
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

constexpr int daysInMonth(int year, int month) {
  switch (month) {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12: return 31;
    case 4: case 6: case 9: case 11: return 30;
    case 2: return isLeapYear(year) ? 29 : 28;
    default: return 0;
  }
}

// Days since 1970-01-01.  Howard Hinnant's days_from_civil: exact for the whole
// proleptic Gregorian range, no loops, no floating point.
constexpr std::int64_t daysFromCivil(const Date& d) {
  std::int64_t y = d.year;
  y -= d.month <= 2;
  const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
  const std::int64_t yoe = y - era * 400;                              // 0..399
  const std::int64_t doy =
      (153 * (d.month + (d.month > 2 ? -3 : 9)) + 2) / 5 + d.day - 1;  // 0..365
  const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;      // 0..146096
  return era * 146097 + doe - 719468;
}

constexpr Date civilFromDays(std::int64_t z) {
  z += 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const std::int64_t doe = z - era * 146097;
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t y = yoe + era * 400;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::int64_t mp = (5 * doy + 2) / 153;
  const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const std::int64_t m = mp + (mp < 10 ? 3 : -9);
  return Date{int(y + (m <= 2)), int(m), int(d)};
}

// 0 = Monday .. 6 = Sunday.  Monday-first matches Chinese calendar convention.
constexpr int weekdayMondayFirst(const Date& d) {
  const std::int64_t days = daysFromCivil(d);
  // 1970-01-01 was a Thursday = index 3 in a Monday-first week.
  const std::int64_t w = (days + 3) % 7;
  return int(w < 0 ? w + 7 : w);
}

inline Date addMonths(const Date& d, int delta) {
  int m = d.month - 1 + delta;
  int y = d.year + (m >= 0 ? m / 12 : (m - 11) / 12);
  m = m % 12;
  if (m < 0) m += 12;
  Date out{y, m + 1, d.day};
  // Clamp so 1-31 minus one month lands on the last of February, not on a
  // nonexistent 31st that later arithmetic would silently roll over.
  const int dim = daysInMonth(out.year, out.month);
  if (out.day > dim) out.day = dim;
  return out;
}

inline Date addDays(const Date& d, int delta) {
  return civilFromDays(daysFromCivil(d) + delta);
}

// "2026-08-02"
inline std::string toIsoString(const Date& d) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", d.year, d.month, d.day);
  return buf;
}

// TIMEZONE POLICY: this treats `ms` as UTC and does NO conversion.
//
// GeeyoouUI deliberately ships no timezone database -- one would be a large
// data dependency, and a plant HMI runs at a single fixed site anyway.  The
// application decides: either pass already-localised milliseconds, or add its
// own offset before calling.  Getting this wrong shows up as timestamps that
// are a whole number of hours off, which is at least an obvious bug rather
// than a subtle one.
inline Date fromUnixMillis(std::uint64_t ms) {
  return civilFromDays(std::int64_t(ms / 86400000ULL));
}

}  // namespace geeyoou
