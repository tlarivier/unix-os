#include <stddef.h>
#include <stdint.h>

typedef long time_t;

struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

static struct tm _tm_buf;

struct tm *localtime(const time_t *timep) {
    time_t t = *timep;
    _tm_buf.tm_sec = t % 60; t /= 60;
    _tm_buf.tm_min = t % 60; t /= 60;
    _tm_buf.tm_hour = t % 24; t /= 24;
    _tm_buf.tm_wday = (t + 4) % 7;
    
    int year = 1970;
    while (1) {
        int days = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (t < days) break;
        t -= days;
        year++;
    }
    _tm_buf.tm_year = year - 1900;
    _tm_buf.tm_yday = t;
    
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    int mon = 0;
    while (mon < 12) {
        int d = mdays[mon] + (mon == 1 && leap);
        if (t < d) break;
        t -= d;
        mon++;
    }
    _tm_buf.tm_mon = mon;
    _tm_buf.tm_mday = t + 1;
    _tm_buf.tm_isdst = 0;
    return &_tm_buf;
}

struct tm *gmtime(const time_t *timep) { return localtime(timep); }

time_t mktime(struct tm *tm) {
    time_t t = 0;
    for (int y = 1970; y < tm->tm_year + 1900; y++) {
        t += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = ((tm->tm_year + 1900) % 4 == 0);
    for (int m = 0; m < tm->tm_mon; m++) {
        t += mdays[m] + (m == 1 && leap);
    }
    t += tm->tm_mday - 1;
    t = t * 24 + tm->tm_hour;
    t = t * 60 + tm->tm_min;
    t = t * 60 + tm->tm_sec;
    return t;
}

static size_t snprintf_num(char *s, size_t max, int val, int width) {
    char buf[16];
    int i = 0;
    if (val == 0) buf[i++] = '0';
    else while (val) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i < width) buf[i++] = '0';
    size_t j = 0;
    while (i > 0 && j < max) s[j++] = buf[--i];
    return j;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    size_t i = 0;
    while (*format && i < max - 1) {
        if (*format == '%' && format[1]) {
            format++;
            switch (*format) {
                case 'Y': i += snprintf_num(s + i, max - i, tm->tm_year + 1900, 4); break;
                case 'm': i += snprintf_num(s + i, max - i, tm->tm_mon + 1, 2); break;
                case 'd': i += snprintf_num(s + i, max - i, tm->tm_mday, 2); break;
                case 'H': i += snprintf_num(s + i, max - i, tm->tm_hour, 2); break;
                case 'M': i += snprintf_num(s + i, max - i, tm->tm_min, 2); break;
                case 'S': i += snprintf_num(s + i, max - i, tm->tm_sec, 2); break;
                default: s[i++] = *format;
            }
            format++;
        } else {
            s[i++] = *format++;
        }
    }
    s[i] = '\0';
    return i;
}

double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}

int settimeofday(const void *tv, const void *tz) {
    (void)tv; (void)tz;
    return -1;
}
