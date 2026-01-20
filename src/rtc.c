#include "rtc.h"
#include "irq/io.h"
#include "console.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

/* RTC registers in CMOS */
#define RTC_SECOND   0x00
#define RTC_MINUTE   0x02
#define RTC_HOUR     0x04
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

static rtc_time_t boot_time;

static inline uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDRESS, reg | 0x80);  /* 0x80 = NMI disable */
    return inb(CMOS_DATA);
}

static int rtc_is_updating(void) {
    outb(CMOS_ADDRESS, RTC_STATUS_A | 0x80);
    return (inb(CMOS_DATA) & 0x80);
}

void rtc_init(void) {
    while (rtc_is_updating());
    rtc_read_time(&boot_time);
}

void rtc_read_time(rtc_time_t *time)
{
    if (!time) return;

    rtc_time_t t1, t2;

    do {
        while (rtc_is_updating());

        t1.second = cmos_read(RTC_SECOND);
        t1.minute = cmos_read(RTC_MINUTE);
        t1.hour   = cmos_read(RTC_HOUR);
        t1.day    = cmos_read(RTC_DAY);
        t1.month  = cmos_read(RTC_MONTH);
        t1.year   = cmos_read(RTC_YEAR);

        while (rtc_is_updating());

        t2.second = cmos_read(RTC_SECOND);
        t2.minute = cmos_read(RTC_MINUTE);
        t2.hour   = cmos_read(RTC_HOUR);
        t2.day    = cmos_read(RTC_DAY);
        t2.month  = cmos_read(RTC_MONTH);
        t2.year   = cmos_read(RTC_YEAR);

    } while (
        t1.second != t2.second ||
        t1.minute != t2.minute ||
        t1.hour   != t2.hour   ||
        t1.day    != t2.day    ||
        t1.month  != t2.month  ||
        t1.year   != t2.year
    );

    uint8_t status_b = cmos_read(RTC_STATUS_B);
    int binary_mode = status_b & 0x04;

    uint8_t sec   = t1.second;
    uint8_t min   = t1.minute;
    uint8_t hr    = t1.hour;
    uint8_t day   = t1.day;
    uint8_t month = t1.month;
    uint8_t year  = t1.year;

    /* Check 12h mode and PM bit before BCD conversion */
    int is_12h = !(status_b & 0x02);
    int is_pm = hr & 0x80;
    hr &= 0x7F;  /* Clear PM bit before conversion */

    if (!binary_mode) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hr    = bcd_to_bin(hr);
        day   = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year  = bcd_to_bin(year);
    }

    /* Handle 12h to 24h conversion */
    if (is_12h) {
        if (hr == 12) {
            hr = is_pm ? 12 : 0;  /* 12 PM = 12, 12 AM = 0 */
        } else if (is_pm) {
            hr += 12;  /* 1-11 PM -> 13-23 */
        }
    }

    time->second = sec;
    time->minute = min;
    time->hour   = hr;
    time->day    = day;
    time->month  = month;
    time->year   = (uint16_t)year + 2000;
}