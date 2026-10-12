#include "calendar_math.h"

bool calendar_is_leap(u32 year){
    return (year%4u==0u && year%100u!=0u) || year%400u==0u;
}
u32 calendar_month_days(u32 year,u32 month){
    static const u8 days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(month<1u || month>12u)return 0;
    return (month==2u && calendar_is_leap(year))?29u:days[month-1u];
}
/* Sakamoto's Gregorian weekday formula. Sunday=0 -> Monday=0.
 * All displayed RTC dates use years >= 2000. */
u32 calendar_weekday_monday(u32 year,u32 month,u32 day){
    static const u8 shift[12]={0,3,2,5,0,3,5,1,4,6,2,4};
    if(year<1600u || month<1u || month>12u || day<1u ||
       day>calendar_month_days(year,month))return 0u;
    u32 y=year;
    if(month<3u)y--;
    u32 sun=(y+y/4u-y/100u+y/400u+shift[month-1u]+day)%7u;
    return (sun+6u)%7u;
}
void calendar_shift_month(u32 year,u32 month,i32 offset,u32 *out_year,u32 *out_month){
    if(!out_year || !out_month)return;
    if(year<1600u || year>9999u)year=2026u;
    if(month<1u || month>12u)month=1u;
    /* Limit by the view's +/-24 month scroll; do not risk unsigned underflow. */
    if(offset<-120)offset=-120;
    if(offset>120)offset=120;
    i32 m=(i32)month-1+offset;
    i32 y=(i32)year;
    while(m<0){m+=12;y--;}
    while(m>=12){m-=12;y++;}
    if(y<1600){y=1600;m=0;}
    if(y>9999){y=9999;m=11;}
    *out_year=(u32)y;
    *out_month=(u32)(m+1);
}
