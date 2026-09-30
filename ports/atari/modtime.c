// Port-specific time functions for Atari TOS / EmuTOS.
// Included by extmod/modtime.c via MICROPY_PY_TIME_INCLUDEFILE.
//
// The clock comes from GEMDOS Tgetdate/Tgettime, which already returns local
// time -- fed by the hardware RTC when EmuTOS or an RTC driver is in place.
// mp_hal_time_ns() does the conversion to a UTC epoch, so we go through it
// rather than MiNTlib's time()/gmtime(), whose TZ handling we would then
// have to second-guess.

#include <stdio.h>      // snprintf, used by strftime below
#include <string.h>
#include "shared/timeutils/timeutils.h"

// Get the local time (UTC + timezone offset).
static void mp_time_localtime_get(timeutils_struct_time_t *tm) {
    mp_timestamp_t t = (mp_timestamp_t)(mp_hal_time_ns() / 1000000000ULL)
                     + mp_hal_timezone_offset_s();
    timeutils_seconds_since_epoch_to_struct_time(t, tm);
}

// Return the number of seconds since the Epoch (UTC, no timezone).
static mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_int((mp_int_t)(mp_hal_time_ns() / 1000000000ULL));
}

// time.strftime(fmt[, t]) — format a time tuple as a string.
// If t is None or omitted, uses localtime().
static mp_obj_t mp_time_strftime(size_t n_args, const mp_obj_t *args) {
    const char *fmt = mp_obj_str_get_str(args[0]);

    int year, month, day, hour, minute, second, weekday, yearday;

    if (n_args < 2 || args[1] == mp_const_none) {
        // Use current local time
        timeutils_struct_time_t tm;
        mp_time_localtime_get(&tm);
        year = tm.tm_year; month = tm.tm_mon; day = tm.tm_mday;
        hour = tm.tm_hour; minute = tm.tm_min; second = tm.tm_sec;
        weekday = tm.tm_wday; yearday = tm.tm_yday;
    } else {
        // Extract from tuple
        mp_obj_t *items;
        size_t len;
        mp_obj_tuple_get(args[1], &len, &items);
        year = mp_obj_get_int(items[0]);
        month = mp_obj_get_int(items[1]);
        day = mp_obj_get_int(items[2]);
        hour = mp_obj_get_int(items[3]);
        minute = mp_obj_get_int(items[4]);
        second = mp_obj_get_int(items[5]);
        weekday = mp_obj_get_int(items[6]);
        yearday = (len > 7) ? mp_obj_get_int(items[7]) : 0;
    }

    static const char *month_names[] = {"", "January", "February", "March",
        "April", "May", "June", "July", "August", "September",
        "October", "November", "December"};
    static const char *month_abbr[] = {"", "Jan", "Feb", "Mar", "Apr", "May",
        "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *day_names[] = {"Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday", "Sunday"};
    static const char *day_abbr[] = {"Mon", "Tue", "Wed", "Thu", "Fri",
        "Sat", "Sun"};

    vstr_t vstr;
    vstr_init(&vstr, 32);

    size_t i = 0;
    size_t fmtlen = strlen(fmt);
    while (i < fmtlen) {
        if (fmt[i] == '%' && i + 1 < fmtlen) {
            char c = fmt[i + 1];
            char buf[16];
            switch (c) {
                case 'Y': snprintf(buf, sizeof(buf), "%04d", year); vstr_add_str(&vstr, buf); break;
                case 'y': snprintf(buf, sizeof(buf), "%02d", year % 100); vstr_add_str(&vstr, buf); break;
                case 'm': snprintf(buf, sizeof(buf), "%02d", month); vstr_add_str(&vstr, buf); break;
                case 'd': snprintf(buf, sizeof(buf), "%02d", day); vstr_add_str(&vstr, buf); break;
                case 'H': snprintf(buf, sizeof(buf), "%02d", hour); vstr_add_str(&vstr, buf); break;
                case 'M': snprintf(buf, sizeof(buf), "%02d", minute); vstr_add_str(&vstr, buf); break;
                case 'S': snprintf(buf, sizeof(buf), "%02d", second); vstr_add_str(&vstr, buf); break;
                case 'j': snprintf(buf, sizeof(buf), "%03d", yearday); vstr_add_str(&vstr, buf); break;
                case 'A': vstr_add_str(&vstr, day_names[weekday % 7]); break;
                case 'a': vstr_add_str(&vstr, day_abbr[weekday % 7]); break;
                case 'B': if (month >= 1 && month <= 12) vstr_add_str(&vstr, month_names[month]); break;
                case 'b': if (month >= 1 && month <= 12) vstr_add_str(&vstr, month_abbr[month]); break;
                case 'p': vstr_add_str(&vstr, hour < 12 ? "AM" : "PM"); break;
                case 'I': { int h = hour % 12; snprintf(buf, sizeof(buf), "%02d", h ? h : 12); vstr_add_str(&vstr, buf); break; }
                case '%': vstr_add_char(&vstr, '%'); break;
                default: vstr_add_char(&vstr, '%'); vstr_add_char(&vstr, c); break;
            }
            i += 2;
        } else {
            vstr_add_char(&vstr, fmt[i]);
            i++;
        }
    }

    return mp_obj_new_str_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_time_strftime_obj, 1, 2, mp_time_strftime);
