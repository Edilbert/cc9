#include <time.h>
#include <stdio.h>
#include <string.h>

unsigned long __jiffies(void);

clock_t clock(void) { return (clock_t)__jiffies(); }

time_t time(time_t *timer)
{
   /* no calendar time available: C89 requires (time_t)-1 */
   if (timer) *timer = (time_t)-1;
   return (time_t)-1;
}

double difftime(time_t time1, time_t time0) { return (double)(time1 - time0); }

static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static const int mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

struct tm *gmtime(const time_t *timer)
{
   static struct tm tm;
   long t = *timer, days;
   int y = 1970, m = 0;
   if (t < 0) return NULL;
   days = t / 86400L;
   t %= 86400L;
   tm.tm_hour = (int)(t / 3600);
   tm.tm_min = (int)(t / 60 % 60);
   tm.tm_sec = (int)(t % 60);
   tm.tm_wday = (int)((days + 4) % 7);
   for (;;)
   {
      int n = is_leap(y) ? 366 : 365;
      if (days < n) break;
      days -= n;
      y++;
   }
   tm.tm_year = y - 1900;
   tm.tm_yday = (int)days;
   for (;;)
   {
      int n = mdays[m] + (m == 1 && is_leap(y));
      if (days < n) break;
      days -= n;
      m++;
   }
   tm.tm_mon = m;
   tm.tm_mday = (int)days + 1;
   tm.tm_isdst = 0;
   return &tm;
}

struct tm *localtime(const time_t *timer) { return gmtime(timer); }

time_t mktime(struct tm *tp)
{
   long days = 0, t;
   int y, m, i;
   /* normalize */
   m = tp->tm_mon;
   y = tp->tm_year + 1900;
   while (m < 0) { m += 12; y--; }
   while (m >= 12) { m -= 12; y++; }
   if (y < 1970) return (time_t)-1;
   for (i = 1970; i < y; i++) days += is_leap(i) ? 366 : 365;
   for (i = 0; i < m; i++) days += mdays[i] + (i == 1 && is_leap(y));
   days += tp->tm_mday - 1;
   t = days * 86400L + tp->tm_hour * 3600L + tp->tm_min * 60L + tp->tm_sec;
   if (t < 0) return (time_t)-1;
   *tp = *gmtime(&t);
   return t;
}

static const char *wday_name[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *mon_name[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *wday_full[7] = { "Sunday", "Monday", "Tuesday", "Wednesday",
                                    "Thursday", "Friday", "Saturday" };
static const char *mon_full[12] = { "January", "February", "March", "April", "May",
   "June", "July", "August", "September", "October", "November", "December" };

char *asctime(const struct tm *tp)
{
   static char buf[26];
   sprintf(buf, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n",
           wday_name[tp->tm_wday], mon_name[tp->tm_mon], tp->tm_mday,
           tp->tm_hour, tp->tm_min, tp->tm_sec, 1900 + tp->tm_year);
   return buf;
}

char *ctime(const time_t *timer)
{
   struct tm *tp = localtime(timer);
   return tp ? asctime(tp) : NULL;
}

size_t strftime(char *s, size_t maxsize, const char *fmt, const struct tm *tp)
{
   size_t n = 0;
   char buf[64];
   for (; *fmt; fmt++)
   {
      const char *p = buf;
      if (*fmt != '%') { buf[0] = *fmt; buf[1] = 0; }
      else
      {
         switch (*++fmt)
         {
            case 'a': p = wday_name[tp->tm_wday]; break;
            case 'A': p = wday_full[tp->tm_wday]; break;
            case 'b': p = mon_name[tp->tm_mon]; break;
            case 'B': p = mon_full[tp->tm_mon]; break;
            case 'c': sprintf(buf, "%s %s %2d %02d:%02d:%02d %d", wday_name[tp->tm_wday],
                              mon_name[tp->tm_mon], tp->tm_mday, tp->tm_hour, tp->tm_min,
                              tp->tm_sec, 1900 + tp->tm_year); break;
            case 'd': sprintf(buf, "%02d", tp->tm_mday); break;
            case 'H': sprintf(buf, "%02d", tp->tm_hour); break;
            case 'I': sprintf(buf, "%02d", (tp->tm_hour + 11) % 12 + 1); break;
            case 'j': sprintf(buf, "%03d", tp->tm_yday + 1); break;
            case 'm': sprintf(buf, "%02d", tp->tm_mon + 1); break;
            case 'M': sprintf(buf, "%02d", tp->tm_min); break;
            case 'p': p = tp->tm_hour < 12 ? "AM" : "PM"; break;
            case 'S': sprintf(buf, "%02d", tp->tm_sec); break;
            case 'U': sprintf(buf, "%02d", (tp->tm_yday + 7 - tp->tm_wday) / 7); break;
            case 'w': sprintf(buf, "%d", tp->tm_wday); break;
            case 'W': sprintf(buf, "%02d", (tp->tm_yday + 7 - (tp->tm_wday + 6) % 7) / 7); break;
            case 'x': sprintf(buf, "%02d/%02d/%02d", tp->tm_mon + 1, tp->tm_mday, tp->tm_year % 100); break;
            case 'X': sprintf(buf, "%02d:%02d:%02d", tp->tm_hour, tp->tm_min, tp->tm_sec); break;
            case 'y': sprintf(buf, "%02d", tp->tm_year % 100); break;
            case 'Y': sprintf(buf, "%d", tp->tm_year + 1900); break;
            case 'Z': buf[0] = 0; break;
            case '%': strcpy(buf, "%"); break;
            default: buf[0] = 0; break;
         }
      }
      while (*p)
      {
         if (n + 1 >= maxsize) return 0;
         s[n++] = *p++;
      }
   }
   if (n >= maxsize) return 0;
   s[n] = 0;
   return n;
}
