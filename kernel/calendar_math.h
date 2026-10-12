/* FalconOS V2 native calendar date arithmetic, pure and independently
 * tested. No RTC or GUI dependency. Monday=0, Sunday=6. */
#ifndef FALCON_CALENDAR_MATH_H
#define FALCON_CALENDAR_MATH_H
#include "falcon.h"
bool calendar_is_leap(u32 year);
u32 calendar_month_days(u32 year,u32 month);
u32 calendar_weekday_monday(u32 year,u32 month,u32 day);
void calendar_shift_month(u32 year,u32 month,i32 offset,u32 *out_year,u32 *out_month);
#endif
