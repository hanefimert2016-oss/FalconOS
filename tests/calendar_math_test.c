/* Pure native calendar regression: known Gregorian dates, leap centuries,
 * February lengths and month navigation. No VM required to audit arithmetic. */
#include "calendar_math.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    assert(calendar_is_leap(2000));
    assert(!calendar_is_leap(1900));
    assert(calendar_is_leap(2024));
    assert(!calendar_is_leap(2026));
    assert(calendar_month_days(2024,2)==29);
    assert(calendar_month_days(2026,2)==28);
    assert(calendar_month_days(2026,4)==30);
    assert(calendar_month_days(2026,12)==31);
    assert(calendar_month_days(2026,0)==0);
    assert(calendar_weekday_monday(2026,10,12)==0); /* Monday */
    assert(calendar_weekday_monday(2026,10,11)==6); /* Sunday */
    assert(calendar_weekday_monday(2000,1,1)==5); /* Saturday */
    assert(calendar_weekday_monday(2024,2,29)==3); /* Thursday */
    assert(calendar_weekday_monday(2025,1,1)==2); /* Wednesday */
    u32 y=0,m=0;
    calendar_shift_month(2026,1,-1,&y,&m); assert(y==2025&&m==12);
    calendar_shift_month(2026,12,1,&y,&m); assert(y==2027&&m==1);
    calendar_shift_month(2024,2,0,&y,&m); assert(y==2024&&m==2);
    calendar_shift_month(2026,10,24,&y,&m); assert(y==2028&&m==10);
    puts("PASS: FalconOS V2 real Gregorian calendar");
    return 0;
}
