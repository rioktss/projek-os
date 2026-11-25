#include "time.h"
#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, (uint8_t)(reg | 0x80));
    return inb(0x71);
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (long long)era * 146097LL + (long long)doe - 719468LL;
}

int time_weekday(int year, int month, int day) {
    long long days = days_from_civil(year, month, day);
    int weekday = (int)((days + 4) % 7);
    if (weekday < 0) weekday += 7;
    return weekday;
}

int time_read_rtc(rtc_time_t *out) {
    if (!out) return -1;

    unsigned timeout = 0;
    while ((cmos_read(0x0A) & 0x80) && timeout < 1000000U) ++timeout;

    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t century = cmos_read(0x32);
    uint8_t status_b = cmos_read(0x0B);

    int pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if (!(status_b & 0x04)) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
        century = bcd_to_binary(century);
    }

    if (!(status_b & 0x02)) {
        if (pm && hour < 12) hour = (uint8_t)(hour + 12);
        if (!pm && hour == 12) hour = 0;
    }

    int full_year;
    if (century >= 19 && century <= 99) {
        full_year = (int)century * 100 + (int)year;
    } else {
        full_year = (year < 70) ? 2000 + (int)year : 1900 + (int)year;
    }

    if (month < 1 || month > 12) return -1;
    if (day < 1 || day > 31) return -1;
    if (hour > 23 || minute > 59 || second > 59) return -1;
    if (full_year < 1970 || full_year > 2199) return -1;

    out->year = full_year;
    out->month = (int)month;
    out->day = (int)day;
    out->hour = (int)hour;
    out->minute = (int)minute;
    out->second = (int)second;
    out->weekday = time_weekday(full_year, (int)month, (int)day);
    return 0;
}

long long time_epoch(void) {
    rtc_time_t t;
    if (time_read_rtc(&t) != 0) return -1;
    long long days = days_from_civil(t.year, t.month, t.day);
    return days * 86400LL + (long long)t.hour * 3600LL +
           (long long)t.minute * 60LL + (long long)t.second;
}

static long long boot_epoch = 0;

void time_init(void) {
    boot_epoch = time_epoch();
    if (boot_epoch < 0) boot_epoch = 0;
}

unsigned long long time_uptime_seconds(void) {
    long long now = time_epoch();
    if (now < 0 || boot_epoch <= 0) return 0;
    long long diff = now - boot_epoch;
    return diff > 0 ? (unsigned long long)diff : 0;
}

void time_sleep_seconds(unsigned seconds) {
    if (!seconds) return;
    long long start = time_epoch();
    if (start < 0) return;
    long long target = start + (long long)seconds;
    while (time_epoch() < target) __asm__ volatile ("pause");
}

const char *time_month_name(int month) {
    static const char *names[12] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (month < 1 || month > 12) return "Unknown";
    return names[month - 1];
}

const char *time_day_name(int weekday) {
    static const char *names[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };
    if (weekday < 0 || weekday > 6) return "Unknown";
    return names[weekday];
}

int time_days_in_month(int month, int year) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2) {
        int leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}
