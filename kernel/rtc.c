/* =============================================================================
 *  FalconOS — CMOS / motherboard real-time clock (MC146818 compatible)
 * -----------------------------------------------------------------------------
 *  Reads wall-clock time from the BIOS RTC over CMOS ports 0x70/0x71 and
 *  applies SET.tz_minutes to convert to the user's local time. The PIT
 *  still drives g_ticks (animation pacing, uptime widget); this module
 *  is purely about *what time it is in the world*.
 *
 *  RTC quirks handled here:
 *    - update-in-progress (status reg A bit 7): we wait for it to clear
 *      so we never read a half-incremented field
 *    - BCD vs binary (status reg B bit 2): older firmware still ships
 *      times as BCD; we decode either form
 *    - 12h vs 24h (status reg B bit 1): we normalise to 24h, honouring
 *      the high bit of the hour byte for PM
 *    - century: BIOS rarely exposes the century byte reliably, so we
 *      hard-assume 21st century. FalconOS will start lying about the
 *      year at 2100; we'll cross that bridge then.
 * ============================================================================= */
#include "falcon.h"

static u8 cmos_read(u8 idx)
{
    outb(0x70, idx);
    return inb(0x71);
}

static u8 bcd2bin(u8 v) { return (v & 0x0F) + (v >> 4) * 10; }

static const u8 DAYS_IN_MONTH[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
};

static bool is_leap(u32 y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static u8 month_length(u32 year, u8 month /* 1..12 */)
{
    if (month == 2 && is_leap(year)) return 29;
    return DAYS_IN_MONTH[month - 1];
}

void rtc_now(rtc_time_t *t)
{
    /* Wait until the chip is not in the middle of incrementing a
     * field; otherwise we can read a 59→00 partial transition.       */
    while (cmos_read(0x0A) & 0x80) { /* spin */ }

    u8 sec  = cmos_read(0x00);
    u8 min  = cmos_read(0x02);
    u8 hour = cmos_read(0x04);
    u8 day  = cmos_read(0x07);
    u8 mon  = cmos_read(0x08);
    u8 year = cmos_read(0x09);
    u8 regB = cmos_read(0x0B);

    bool is_bcd = !(regB & 0x04);
    if (is_bcd) {
        sec  = bcd2bin(sec);
        min  = bcd2bin(min);
        u8 hi = hour & 0x80;
        hour  = bcd2bin(hour & 0x7F) | hi;
        day   = bcd2bin(day);
        mon   = bcd2bin(mon);
        year  = bcd2bin(year);
    }

    /* 12h → 24h normalisation: high bit of hour means PM in 12h mode */
    if (!(regB & 0x02)) {
        bool pm = (hour & 0x80) != 0;
        hour &= 0x7F;
        if (hour == 12) hour = 0;
        if (pm)         hour = (hour + 12) % 24;
    }

    if (mon  == 0) mon  = 1;
    if (day  == 0) day  = 1;
    if (mon  > 12) mon  = 12;
    if (year > 99) year = 99;

    t->year  = 2000u + year;
    t->month = mon;
    t->day   = day;
    t->hour  = hour;
    t->min   = min;
    t->sec   = sec;
}

/* Read the RTC and apply SET.tz_minutes to roll the date forward or
 * backward across midnight, month and year boundaries.               */
void rtc_local(rtc_time_t *t)
{
    rtc_now(t);

    i32 total = (i32)t->hour * 60 + (i32)t->min + SET.tz_minutes;
    i32 day_delta = 0;
    while (total <       0) { total += 24 * 60; day_delta--; }
    while (total >= 24 * 60) { total -= 24 * 60; day_delta++; }
    t->hour = (u8)(total / 60);
    t->min  = (u8)(total % 60);

    while (day_delta < 0) {
        day_delta++;
        if (t->day > 1) {
            t->day--;
        } else {
            t->month = (t->month == 1) ? 12 : (t->month - 1);
            if (t->month == 12 && t->day == 1) t->year--;
            t->day = month_length(t->year, t->month);
        }
    }
    while (day_delta > 0) {
        day_delta--;
        u8 dim = month_length(t->year, t->month);
        t->day++;
        if (t->day > dim) {
            t->day = 1;
            t->month++;
            if (t->month > 12) { t->month = 1; t->year++; }
        }
    }
}
