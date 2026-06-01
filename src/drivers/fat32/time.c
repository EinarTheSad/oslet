#include "private.h"

uint16_t rtc_to_fat_time(const rtc_time_t *rtc) {
    return ((uint16_t)(rtc->hour & 0x1F) << 11) |
           ((uint16_t)(rtc->minute & 0x3F) << 5) |
           ((uint16_t)(rtc->second / 2) & 0x1F);
}

uint16_t rtc_to_fat_date(const rtc_time_t *rtc) {
    uint16_t year = (rtc->year >= 1980) ? (rtc->year - 1980) : 0;
    if (year > 127) year = 127;
    return ((year & 0x7F) << 9) |
           ((uint16_t)(rtc->month & 0x0F) << 5) |
           ((uint16_t)(rtc->day & 0x1F));
}

void get_fat_timestamp(uint16_t *time, uint16_t *date) {
    rtc_time_t rtc;
    rtc_read_time(&rtc);
    if (time) *time = rtc_to_fat_time(&rtc);
    if (date) *date = rtc_to_fat_date(&rtc);
}
