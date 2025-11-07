#include "rtc.h"
#include "io.h"
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
    
    printf("RTC initialized: %02u/%02u/%04u %02u:%02u:%02u\n",
           boot_time.day, boot_time.month, boot_time.year,
           boot_time.hour, boot_time.minute, boot_time.second);
}

void rtc_read_time(rtc_time_t *time) {
    if (!time) return;
    
    uint8_t last_second, last_minute, last_hour;
    uint8_t last_day, last_month, last_year;
    
    do {
        while (rtc_is_updating());
        
        last_second = cmos_read(RTC_SECOND);
        last_minute = cmos_read(RTC_MINUTE);
        last_hour   = cmos_read(RTC_HOUR);
        last_day    = cmos_read(RTC_DAY);
        last_month  = cmos_read(RTC_MONTH);
        last_year   = cmos_read(RTC_YEAR);
        
    } while (last_second != cmos_read(RTC_SECOND));
    
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    int binary_mode = status_b & 0x04;
    
    if (!binary_mode) {
        time->second = bcd_to_bin(last_second);
        time->minute = bcd_to_bin(last_minute);
        time->hour   = bcd_to_bin(last_hour);
        time->day    = bcd_to_bin(last_day);
        time->month  = bcd_to_bin(last_month);
        time->year   = bcd_to_bin(last_year);
    } else {
        time->second = last_second;
        time->minute = last_minute;
        time->hour   = last_hour;
        time->day    = last_day;
        time->month  = last_month;
        time->year   = last_year;
    }
    
    time->year += 2000;
    
    /* Check if need to convert to 24h (bit 1 in status_b) */
    int is_12h = !(status_b & 0x02);
    if (is_12h && (last_hour & 0x80)) {
        time->hour = ((time->hour & 0x7F) + 12) % 24;
    }
}

void rtc_print_time(void) {
    rtc_time_t current;
    rtc_read_time(&current);
    
    printf("Current time: %02u/%02u/%04u %02u:%02u:%02u\n",
           current.day, current.month, current.year,
           current.hour, current.minute, current.second);
}