#pragma once

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
} rtc_time_t;

int time_read_rtc(rtc_time_t *out);
long long time_epoch(void);
int time_weekday(int year, int month, int day);
void time_init(void);
unsigned long long time_uptime_seconds(void);
void time_sleep_seconds(unsigned seconds);
const char *time_month_name(int month);
const char *time_day_name(int weekday);
int time_days_in_month(int month, int year);
