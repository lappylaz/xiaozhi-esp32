#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "sabrina_clock.h"

void check(const char* utc, const char* expected, int daylight) {
    tm parsed = {};
    assert(strptime(utc, "%Y-%m-%d %H:%M", &parsed));
    time_t epoch = timegm(&parsed);
    tm eastern = {};
    assert(localtime_r(&epoch, &eastern));
    char text[16];
    sabrina::FormatClock(eastern, text);
    assert(strcmp(text, expected) == 0);
    assert(eastern.tm_isdst == daylight);
}

int main() {
    assert(setenv("TZ", sabrina::kEasternTimezone, 1) == 0);
    tzset();
    check("2026-10-12 04:00", "12:00 AM", 1);
    check("2026-01-12 05:00", "12:00 AM", 0);
    check("2026-07-12 16:00", "12:00 PM", 1);
    check("2026-03-08 06:59", "1:59 AM", 0);
    check("2026-03-08 07:00", "3:00 AM", 1);
    check("2026-11-01 05:59", "1:59 AM", 1);
    check("2026-11-01 06:00", "1:00 AM", 0);
    std::cout << "PASS: Eastern midnight/noon, winter/summer and both DST transitions\n";
}
