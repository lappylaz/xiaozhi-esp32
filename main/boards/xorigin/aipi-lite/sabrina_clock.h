#pragma once

#include <cstdio>
#include <ctime>

namespace sabrina {
inline constexpr char kEasternTimezone[] = "EST5EDT,M3.2.0/2,M11.1.0/2";

// Separate local clock text from voice state; never replace a listening/error label.
inline void FormatClock(const tm& local, char (&text)[16]) {
    const int hour = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    std::snprintf(text, sizeof(text), "%d:%02d %s", hour, local.tm_min,
                  local.tm_hour < 12 ? "AM" : "PM");
}
}  // namespace sabrina
