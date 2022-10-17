/*
   Copyright (c) 2000, 2018, Oracle and/or its affiliates.
   Copyright (c) 2009, 2019, MariaDB Corporation.
   Copyright (c) 2021 OceanBase Technology Co.,Ltd.
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA */
#include <ma_global.h>
#include <ma_sys.h>
#include "ob_oralce_format_models.h"
#include "mariadb_ctype.h"
#include <ma_string.h>
#include "ob_bitmap.h"

int32_t calc_max_name_length(const struct ObTimeConstStr names[], const int64_t size);

#define FALSE_IT(stmt) ({ (stmt); FALSE; })

#define INT32_MAX_DIGITS_LEN   10
#define EPOCH_YEAR4   1970
#define EPOCH_WDAY    4       // 1970-1-1 is thursday.
#define LEAP_YEAR_COUNT(y)  ((y) / 4 - (y) / 100 + (y) / 400)
#define IS_LEAP_YEAR(y) ((((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0)) ? 1 : 0)

#define DAYS_PER_NYEAR  365
#define DAYS_PER_LYEAR  366

static const int32_t MIN_OFFSET_MINUTES = -15 * 60 - 59;
static const int32_t MAX_OFFSET_MINUTES = 15 * 60 + 59;
static const int32_t ZERO_DATE = (int32_t)(-106751991);
static const int64_t ZERO_TIME = 0;
static const int64_t MAX_VARCHAR_LENGTH = 1024L * 1024L; // unit is byte
static const int64_t COMMON_ELEMENT_NUMBER = 10;
static const int16_t MAX_SCALE_FOR_ORACLE_TEMPORAL = 9;
static const int16_t DEFAULT_SCALE_FOR_ORACLE_FRACTIONAL_SECONDS = 6;
static const int64_t UNKNOWN_LENGTH_OF_ELEMENT = 20;

static const int64_t power_of_10[INT32_MAX_DIGITS_LEN] = {
    1L,
    10L,
    100L,
    1000L,
    10000L,
    100000L,
    1000000L,
    10000000L,
    100000000L,
    1000000000L,
    //2147483647
};

const int64_t TZ_PART_MIN[DATETIME_PART_CNT]  = {   1,  1,  1,  0,  0,  0, 0};
const int64_t TZ_PART_MAX[DATETIME_PART_CNT]  = {9999, 12, 31, 23, 59, 59, 1000000000};

static const int8_t DAYS_PER_MON[2][12 + 1] = {
  {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
  {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

static const int32_t DAYS_UNTIL_MON[2][12 + 1] = {
  {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
  {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}
};

/**
 * 3 days after wday 5 is wday 1, so [3][5] = 1.
 * 5 days before wday 2 is wday 4, so [-5][2] = 4.
 * and so on, max wday is 7, min offset is -6, max offset days is 6.
 */
static const int8_t WDAY_OFFSET_ARR[DAYS_PER_WEEK * 2 - 1][DAYS_PER_WEEK + 1] = {
  {0, 2, 3, 4, 5, 6, 7, 1},
  {0, 3, 4, 5, 6, 7, 1, 2},
  {0, 4, 5, 6, 7, 1, 2, 3},
  {0, 5, 6, 7, 1, 2, 3, 4},
  {0, 6, 7, 1, 2, 3, 4, 5},
  {0, 7, 1, 2, 3, 4, 5, 6},   // offset = -1, wday = 1/2/3/4/5/6/7.
  {0, 1, 2, 3, 4, 5, 6, 7},   // offset =  0, wday = 1/2/3/4/5/6/7.
  {0, 2, 3, 4, 5, 6, 7, 1},   // offset =  1, wday = 1/2/3/4/5/6/7.
  {0, 3, 4, 5, 6, 7, 1, 2},
  {0, 4, 5, 6, 7, 1, 2, 3},
  {0, 5, 6, 7, 1, 2, 3, 4},
  {0, 6, 7, 1, 2, 3, 4, 5},
  {0, 7, 1, 2, 3, 4, 5, 6}
};

static const int8_t (*WDAY_OFFSET)[DAYS_PER_WEEK + 1] = &WDAY_OFFSET_ARR[6];

/*
    * if wday of yday 1 is 4, not SUN_BEGIN, not GE_4_BEGIN, yday of fitst day of week 1 is 5, so [4][0][0] = 5.
     * if wday of yday 1 is 2,     SUN_BEGIN,     GE_4_BEGIN, yday of fitst day of week 1 is 5, so [2][1][1] = 1.
      * and so on, max wday is 7, and other two is 1.
       * ps: if the first week is not full week(GE_4_BEGIN), the yday maybe zero or neg, such as 0 means
        *     the last day of prev year, and so on.
         */
static const int8_t YDAY_WEEK1[DAYS_PER_WEEK + 1][2][2] = {
  {{0,  0}, {0,  0}},
  {{1,  1}, {7,  0}},  // wday of day 1 is 1.
  {{7,  0}, {6, -1}},  // 2.
  {{6, -1}, {5, -2}},  // 3.
  {{5, -2}, {4,  4}},  // 4.
  {{4,  4}, {3,  3}},  // 5.
  {{3,  3}, {2,  2}},  // 6.
  {{2,  2}, {1,  1}}   // 7.
};

#define WEEK_MODE_CNT   8
static const ObDTMode WEEK_MODE[WEEK_MODE_CNT] = {
  DT_WEEK_SUN_BEGIN | DT_WEEK_ZERO_BEGIN                       ,
                      DT_WEEK_ZERO_BEGIN | DT_WEEK_GE_4_BEGIN  ,
  DT_WEEK_SUN_BEGIN                                            ,
                                           DT_WEEK_GE_4_BEGIN  ,  //ISO-8601 standard week
  DT_WEEK_SUN_BEGIN | DT_WEEK_ZERO_BEGIN | DT_WEEK_GE_4_BEGIN  ,
                      DT_WEEK_ZERO_BEGIN                       ,
  DT_WEEK_SUN_BEGIN |                      DT_WEEK_GE_4_BEGIN  ,
                                                            0
};

static const int32_t DAYS_PER_YEAR[2]=
{
  DAYS_PER_NYEAR, DAYS_PER_LYEAR
};

static const struct ObTimeConstStr WDAY_NAMES[DAYS_PER_WEEK + 1] = {
    {"null", 4},
    {"Monday", 6}, {"Tuesday", 7}, {"Wednesday", 9}, {"Thursday", 8}, {"Friday", 6}, {"Saturday", 8}, {"Sunday", 6}
};

static const int32_t MAX_WDAY_NAME_LENGTH = 9;

static const struct ObTimeConstStr WDAY_ABBR_NAMES[DAYS_PER_WEEK + 1] = {
    {"null", 4},
    {"Mon", 3}, {"Tue", 3}, {"Wed", 3}, {"Thu", 3}, {"Fri", 3}, {"Sat", 3}, {"Sun", 3}
};
static const struct ObTimeConstStr MON_NAMES[12 + 1] = {
    {"null", 4},
    {"January", 7}, {"February", 8}, {"March", 5}, {"April", 5}, {"May", 3}, {"June", 4},
    {"July", 4}, {"August", 6}, {"September", 9}, {"October", 7}, {"November", 8}, {"December", 8}
};

static const int32_t MAX_MON_NAME_LENGTH = 9;

static const struct ObTimeConstStr MON_ABBR_NAMES[12 + 1] = {
    {"null", 4},
    {"Jan", 3}, {"Feb", 3}, {"Mar", 3}, {"Apr", 3}, {"May", 3}, {"Jun", 3},
    {"Jul", 3}, {"Aug", 3}, {"Sep", 3}, {"Oct", 3}, {"Nov", 3}, {"Dec", 3}
};

static int64_t  CONFLICT_GROUP_MAP[MAX_FLAG_NUMBER] =
{
  /**AD*/         ERA_GROUP,
  /**AD2*/        ERA_GROUP,
  /**BC*/         ERA_GROUP,
  /**BC2*/        ERA_GROUP,
  /**CC,*/        NEVER_APPEAR_GROUP,
  /**SCC*/        NEVER_APPEAR_GROUP,
  /**D*/          WEEK_OF_DAY_GROUP,
  /**DAY*/        WEEK_OF_DAY_GROUP,
  /**DD*/         NON_CONFLICT_GROUP,
  /**DDD*/        DAY_OF_YEAR_GROUP,
  /**DY*/         WEEK_OF_DAY_GROUP,
  /**FF1*/        NON_CONFLICT_GROUP,
  /**FF2*/        NON_CONFLICT_GROUP,
  /**FF3*/        NON_CONFLICT_GROUP,
  /**FF4*/        NON_CONFLICT_GROUP,
  /**FF5*/        NON_CONFLICT_GROUP,
  /**FF6*/        NON_CONFLICT_GROUP,
  /**FF7*/        NON_CONFLICT_GROUP,
  /**FF8*/        NON_CONFLICT_GROUP,
  /**FF9*/        NON_CONFLICT_GROUP,
  /**FF*/         NON_CONFLICT_GROUP,
  /**HH*/         HOUR_GROUP,
  /**HH24*/       HOUR_GROUP,
  /**HH12*/       HOUR_GROUP,
  /**IW*/         NEVER_APPEAR_GROUP,
  /**I*/          NEVER_APPEAR_GROUP,
  /**IY*/         NEVER_APPEAR_GROUP,
  /**IYY*/        NEVER_APPEAR_GROUP,
  /**IYYY*/       NEVER_APPEAR_GROUP,
  /**MI*/         NON_CONFLICT_GROUP,
  /**MM*/         MONTH_GROUP,
  /**MONTH*/      MONTH_GROUP,
  /**MON*/        MONTH_GROUP,
  /**AM*/         MERIDIAN_INDICATOR_GROUP,
  /**AM2*/        MERIDIAN_INDICATOR_GROUP,
  /**PM*/         MERIDIAN_INDICATOR_GROUP,
  /**PM2*/        MERIDIAN_INDICATOR_GROUP,
  /**Q*/          NON_CONFLICT_GROUP,
  /**RR*/         YEAR_GROUP,
  /**RRRR*/       YEAR_GROUP,
  /**SS*/         NON_CONFLICT_GROUP,
  /**SSSSS*/      RUNTIME_CONFLICT_SOLVE_GROUP,
  /**WW*/         NEVER_APPEAR_GROUP,
  /**W*/          NEVER_APPEAR_GROUP,
  /**YGYYY*/      YEAR_GROUP,
  /**YEAR*/       NEVER_APPEAR_GROUP,
  /**SYEAR*/      NEVER_APPEAR_GROUP,
  /**YYYY*/       YEAR_GROUP,
  /**SYYYY*/      YEAR_GROUP,
  /**YYY*/        YEAR_GROUP,
  /**YY*/         YEAR_GROUP,
  /**Y*/          YEAR_GROUP,
  /**DS*/         RUNTIME_CONFLICT_SOLVE_GROUP,
  /**DL*/         RUNTIME_CONFLICT_SOLVE_GROUP,
  /**TZH*/        RUNTIME_CONFLICT_SOLVE_GROUP,
  /**TZM*/        RUNTIME_CONFLICT_SOLVE_GROUP,
  /**TZD*/        RUNTIME_CONFLICT_SOLVE_GROUP,
  /**TZR*/        RUNTIME_CONFLICT_SOLVE_GROUP,
  /**X*/          NON_CONFLICT_GROUP,
  /**J*/          DAY_OF_YEAR_GROUP
};

//define the length ,may be less ,but should not max than this
static int64_t EXPECTED_MATCHING_LENGTH[MAX_FLAG_NUMBER] =
{
  /**AD*/         2,
  /**AD2*/        4,
  /**BC*/         2,
  /**BC2*/        4,
  /**CC,*/        0, //never used
  /**SCC*/        0, //never used
  /**D*/          1,
  /**DAY*/        0, //non-numeric, ignored
  /**DD*/         2,
  /**DDD*/        3,
  /**DY*/         0, //non-numeric, ignored
  /**FF1*/        1,
  /**FF2*/        2,
  /**FF3*/        3,
  /**FF4*/        4,
  /**FF5*/        5,
  /**FF6*/        6,
  /**FF7*/        7,
  /**FF8*/        8,
  /**FF9*/        9,
  /**FF*/         9,
  /**HH*/         2,
  /**HH24*/       2,
  /**HH12*/       2,
  /**IW*/         0, //never used
  /**I*/          0, //never used
  /**IY*/         0, //never used
  /**IYY*/        0, //never used
  /**IYYY*/       0, //never used
  /**MI*/         2,
  /**MM*/         2,
  /**MONTH*/      0, //non-numeric, ignored
  /**MON*/        0, //non-numeric, ignored
  /**AM*/         2,
  /**AM2*/        4,
  /**PM*/         2,
  /**PM2*/        4,
  /**Q*/          0, //never used
  /**RR*/         0, //special case
  /**RRRR*/       0, //special case
  /**SS*/         2,
  /**SSSSS*/      5,
  /**WW*/         0, //never used
  /**W*/          0, //never used
  /**YGYYY*/      5,
  /**YEAR*/       0, //never used
  /**SYEAR*/      0, //never used
  /**YYYY*/       4,
  /**SYYYY*/      4,
  /**YYY*/        3,
  /**YY*/         2,
  /**Y*/          1,
  /**DS*/         0, //todo
  /**DL*/         0, //todo
  /**TZH*/        2, //todo
  /**TZM*/        2, //todo
  /**TZD*/        0, //non-numeric, ignored
  /**TZR*/        0, //non-numeric, ignored
  /**X*/          0, //non-numeric, ignored
  /**J*/          7
};

int32_t calc_max_name_length(const struct ObTimeConstStr names[], const int64_t size)
{
    int32_t res = 0;
    int64_t i;
    for (i = 1; i <= size; ++i) {
        if (res < names[i].len_) {
            res = names[i].len_;
    }
  }
  return res;
}

int databuff_vprintf(char *buf, const int64_t buf_len, int64_t *pos, const char *fmt, va_list args)
{
  int ret = 0;
  if (NULL != buf && 0 <= *pos && *pos < buf_len) {
    int len = vsnprintf(buf + *pos, buf_len - *pos, fmt, args);
    if (len < 0) {
      ret = 1;
    } else if (len < buf_len - *pos) {
      *pos += len;
    } else {
      *pos = buf_len - 1;  //skip '\0' written by vsnprintf
      ret = 1;
    }
  } else {
    ret = 1;
  }
  return ret;
}

int databuff_printf(char *buf, const int64_t buf_len, int64_t *pos, const char *fmt, ...)
{
  int ret = 0;
  va_list args;
  va_start(args, fmt);
  if (0 != (ret = databuff_vprintf(buf, buf_len, pos, fmt, args))) {
  } else {}
  va_end(args);
  return ret;
}

//PATTERN in windows(wingdi.h)Duplicate definition,So change PATTERN to PATTERNEX
//do not define duplicate pattern
const struct ObTimeConstStr PATTERNEX[MAX_FLAG_NUMBER] =
{
#define ObTimeConstStr(x) {x, sizeof(x) - 1}
  ObTimeConstStr("AD"),
  ObTimeConstStr("A.D."),
  ObTimeConstStr("BC"),
  ObTimeConstStr("B.C."),
  ObTimeConstStr("CC"),
  ObTimeConstStr("SCC"),
  ObTimeConstStr("D"),
  ObTimeConstStr("DAY"),
  ObTimeConstStr("DD"),
  ObTimeConstStr("DDD"),
  ObTimeConstStr("DY"),
  ObTimeConstStr("FF1"),
  ObTimeConstStr("FF2"),
  ObTimeConstStr("FF3"),
  ObTimeConstStr("FF4"),
  ObTimeConstStr("FF5"),
  ObTimeConstStr("FF6"),
  ObTimeConstStr("FF7"),
  ObTimeConstStr("FF8"),
  ObTimeConstStr("FF9"),
  ObTimeConstStr("FF"),
  ObTimeConstStr("HH"),
  ObTimeConstStr("HH24"),
  ObTimeConstStr("HH12"),
  ObTimeConstStr("IW"),
  ObTimeConstStr("I"),
  ObTimeConstStr("IY"),
  ObTimeConstStr("IYY"),
  ObTimeConstStr("IYYY"),
  ObTimeConstStr("MI"),
  ObTimeConstStr("MM"),
  ObTimeConstStr("MONTH"),
  ObTimeConstStr("MON"),
  ObTimeConstStr("AM"),
  ObTimeConstStr("A.M."),
  ObTimeConstStr("PM"),
  ObTimeConstStr("P.M."),
  ObTimeConstStr("Q"),
  ObTimeConstStr("RR"),
  ObTimeConstStr("RRRR"),
  ObTimeConstStr("SS"),
  ObTimeConstStr("SSSSS"),
  ObTimeConstStr("WW"),
  ObTimeConstStr("W"),
  ObTimeConstStr("Y,YYY"),
  ObTimeConstStr("YEAR"),
  ObTimeConstStr("SYEAR"),
  ObTimeConstStr("YYYY"),
  ObTimeConstStr("SYYYY"),
  ObTimeConstStr("YYY"),
  ObTimeConstStr("YY"),
  ObTimeConstStr("Y"),
  ObTimeConstStr("DS"),
  ObTimeConstStr("DL"),
  ObTimeConstStr("TZH"),
  ObTimeConstStr("TZM"),
  ObTimeConstStr("TZD"),
  ObTimeConstStr("TZR"),
  ObTimeConstStr("X"),
#undef ObTimeConstStr
};

static const struct ObOracleTimeLimiter LIMITER_YEAR                       = {1, 9999,   1};
static const struct ObOracleTimeLimiter LIMITER_MONTH                      = {1, 12,     1};
static const struct ObOracleTimeLimiter LIMITER_MONTH_DAY                  = {1, 31,     1};
static const struct ObOracleTimeLimiter LIMITER_WEEK_DAY                   = {1, 7,      1};
static const struct ObOracleTimeLimiter LIMITER_YEAR_DAY                   = {1, 366,    1};
static const struct ObOracleTimeLimiter LIMITER_HOUR12                     = {1, 12,     1};
static const struct ObOracleTimeLimiter LIMITER_HOUR24                     = {0, 23,     1};
static const struct ObOracleTimeLimiter LIMITER_MINUTE                     = {0, 59,     1};
static const struct ObOracleTimeLimiter LIMITER_SECOND                     = {0, 59,     1};
static const struct ObOracleTimeLimiter LIMITER_SECS_PAST_MIDNIGHT         = {0, 86399,  1};
static const struct ObOracleTimeLimiter LIMITER_TIMEZONE_HOUR_ABS          = {0, 15,     1}; //ORA-01874: time zone hour must be between -15 and 15
static const struct ObOracleTimeLimiter LIMITER_TIMEZONE_MIN_ABS           = {0, 59,     1}; //ORA-01875: time zone minute must be between -59 and 59
static const struct ObOracleTimeLimiter LIMITER_JULIAN_DATE                = {1, 5373484,1}; // -4712-01-01 ~ 9999-12-31

static inline int check_validate(const struct ObOracleTimeLimiter *limiter, int32_t value)
{
  int ret = 0;
  if (value < limiter->MIN || value > limiter->MAX) {
    ret = limiter->ERROR_CODE;
  }
  return ret;
}

int get_day_and_month_from_year_day(const int32_t yday, const int32_t year, int32_t *month, int32_t *day)
{
  int ret = 0;
  int32_t leap_year = IS_LEAP_YEAR(year);
  if (yday > DAYS_UNTIL_MON[leap_year][12]) {
    ret = 1;
  } else {
    my_bool stop_flag = FALSE;
    int32_t i = LIMITER_MONTH.MIN;
    for (; !stop_flag && i <= LIMITER_MONTH.MAX; ++i) {
      if (yday <= DAYS_UNTIL_MON[leap_year][i]) {
        *month = i;
        *day = yday - DAYS_UNTIL_MON[leap_year][i - 1];
        stop_flag = TRUE;
      }
    }
  }
  return ret;
}

int validate_oracle_date(const struct ObTime *ob_time)
{
  const int32_t *parts = ob_time->parts_;
  int ret = 0;
  int i = 0;
  for (; 0 == ret && i < ORACLE_DATE_PART_CNT; ++i) {
    if (parts[i] < TZ_PART_MIN[i] || parts[i] > TZ_PART_MAX[i]) {
      ret = 1;
    }
  }
  if (0 == ret) {
    int is_leap = IS_LEAP_YEAR(parts[DT_YEAR]);
    if (parts[DT_MDAY] > DAYS_PER_MON[is_leap][parts[DT_MON]]) {
      ret = 1;
    }
  }
  return ret;
}

/*
 * if format elements contains TZR
 * hour minuts seconds and fracial second can not omit
 * Because, I guess, the daylight-saving time may be uncertain
 * if the time part is omitted.
 * The day
 */
inline my_bool is_element_can_omit(const struct ObDFMElem *elem)
{
  int ret_bool = TRUE;
  int64_t flag = elem->elem_flag_;
  int64_t conf_group = CONFLICT_GROUP_MAP[flag];
  if (YEAR_GROUP == conf_group
      || WEEK_OF_DAY_GROUP == conf_group
      || MONTH_GROUP == conf_group
      || DD == flag
      || DS == flag
      || DL == flag) {
    ret_bool = FALSE;
  } else {
    //return true
  }
  return ret_bool;
}

static inline my_bool is_sign_char(const char ch) {
  return '-' == ch || '+' == ch;
}

static inline my_bool is_split_char(const char ch)
{
  int ret_bool = FALSE;
  if (ch == '\n' || ch == '\t'
      || ((ch >= 0x20 && ch <= 0x7E) &&
          !((ch >= '0' && ch <= '9')
           || (ch >='a' && ch <= 'z')
           || (ch >= 'A' && ch <= 'Z')))) {
    ret_bool = TRUE;
  }
  return ret_bool;
}

static int date_to_ob_time(int32_t value, struct ObTime *ob_time)
{
  int ret = 0;
  int32_t *parts = ob_time->parts_;
  if (!HAS_TYPE_ORACLE(ob_time->mode_) && ZERO_DATE == value) {
    memset(parts, 0, sizeof(*parts) * DATETIME_PART_CNT);
    parts[DT_DATE] = ZERO_DATE;
  } else {
    int32_t days = value;
    int32_t leap_year = 0;
    int32_t year = EPOCH_YEAR4;
    const int32_t *cur_days_until_mon = NULL;
    int32_t month = 1;
    parts[DT_DATE] = value;
    // year.
    while (days < 0 || days >= DAYS_PER_YEAR[leap_year = IS_LEAP_YEAR(year)]) {
      int32_t new_year = year + days / DAYS_PER_NYEAR;
      new_year -= (days < 0);
      days -= (new_year - year) * DAYS_PER_NYEAR + LEAP_YEAR_COUNT(new_year - 1) - LEAP_YEAR_COUNT(year - 1);
      year = new_year;
    }
    parts[DT_YEAR] = year;
    parts[DT_YDAY] = days + 1;
    parts[DT_WDAY] = WDAY_OFFSET[value % DAYS_PER_WEEK][EPOCH_WDAY];
    // month.
    cur_days_until_mon = DAYS_UNTIL_MON[leap_year];
    for (; month < MONS_PER_YEAR && days >= cur_days_until_mon[month]; ++month) {}
    parts[DT_MON] = month;
    days -= cur_days_until_mon[month - 1];
    // day.
    parts[DT_MDAY] = days + 1;
  }
  return ret;
}

int set_ob_time_part_directly(struct ObTime *ob_time, int64_t *conflict_bitset, const int64_t part_offset, const int32_t part_value)
{
  int ret = 0;
  if (part_offset >= TOTAL_PART_CNT) {
    ret = 1;
  } else {
    ob_time->parts_[part_offset] = part_value;
    *conflict_bitset |= (1 << part_offset);
  }
  return ret;
}

// DT_YEAR use this interface ,as it should treat special：
// select to_date('5', 'YY') from dual;  the result for oracle is 2005-09-01
//
int set_ob_time_year_may_conflict(struct ObTime *ob_time, int32_t *julian_year_value,
                                  int32_t check_year, int32_t set_year,
                                  my_bool overwrite)
{
  int ret = 0;

  if (ZERO_DATE != *julian_year_value) {
    if (*julian_year_value != check_year) {
      ret = 1;
    } else if (overwrite) {
      ob_time->parts_[DT_YEAR] = set_year;
    }
  } else {
    ob_time->parts_[DT_YEAR] = set_year;
    *julian_year_value = check_year;
  }
  return (ret);
}

/*
 * element group may cause conflict on parts in ob_time
 * 1. SSSSS vs HH, HH24, HH12, MI, SS
 * 2. DDD vs DD MM/Mon/Month
 *
 * while call this function, the part_value must be the final value
 */
int set_ob_time_part_may_conflict(struct ObTime *ob_time, int64_t *conflict_bitset, const int64_t part_offset, const int32_t part_value)
{
  int ret = 0;

  if (part_offset >= TOTAL_PART_CNT) {
    ret = 1;
  } else {
    if (0 != (*conflict_bitset & (1 << part_offset))) {
      //already has data in ob_time.part_[part_name], validate it
      if (part_value != ob_time->parts_[part_offset]) {
        ret = 1;
      }
    } else {
      *conflict_bitset |= (1 << part_offset);
      ob_time->parts_[part_offset] = part_value;
    }
  }

  return (ret);
}

int check_int_value_length(const struct ObDFMParseCtx *ctx,
                           const int64_t expected_len,
                           const int64_t real_data_len)
{
  int ret = 0;
  /*
   * format need separate chars but input omit separate chars like:
   * to_date('20181225', 'YYYY-MM-DD')    input omit '-'
   * in this situation, the numeric value are matched in fixed length mode.
   * which means real_data_len should be equal to element expected length, or will return with an error
   */
  if (0 == ret && ctx->is_matching_by_expected_len_) {  //is true only in only in str_to_ob_time_oracle_dfm
    my_bool legal = TRUE;
    if (RR == ctx->expected_elem_flag_ || RRRR == ctx->expected_elem_flag_) { //one special case
      legal = (2 == real_data_len || 4 == real_data_len);
    } else if (expected_len > 0) { //usual case, for numeric value
      legal = (real_data_len == expected_len);
    }

    if (!legal) {
      ret = 1;
    }
  }
  return ret;
}

static int match_int_value_with_comma(struct ObDFMParseCtx *ctx,
                                      const int64_t expected_len,
                                      int64_t *value_len,
                                      int32_t *result)
{
  int ret = 0;
  int32_t temp_value = 0;
  int64_t real_data_len = 0;
  int64_t digits_len = 0;
  int64_t continuous_comma_count = 0;
  my_bool stop_flag = FALSE;


  if (!(ctx->cur_ch_ != NULL && ctx->remain_len_ > 0)) {
    ret = 1;
  }
  while (0 == ret && !stop_flag
         && real_data_len < ctx->remain_len_ && digits_len < expected_len) { //look digits by # of value_len
    char cur_char = *(ctx->cur_ch_ + real_data_len);
    if (',' == cur_char) {
      continuous_comma_count++;
      if (continuous_comma_count == 2) {
        --real_data_len;
        stop_flag = TRUE;
      } else {
        ++real_data_len;
      }
    } else {
      continuous_comma_count = 0;
      if (is_split_char(cur_char)) {
        stop_flag = TRUE;
      } else {
        if (!isdigit(cur_char)) {
          ret = 1; //ORA-01858: a non-numeric character was found where a numeric was expected
        } else {
          temp_value *= 10;
          temp_value += cur_char - '0';
          ++real_data_len;
          ++digits_len;
        }
      }
    }
  }
  if (0 == ret) {
    if (0 != (ret = check_int_value_length(ctx, expected_len, real_data_len))) {
    } else {
      *value_len = real_data_len;
      *result = temp_value;
    }
  }
  return (ret);
}

int match_int_value_with_sign(struct ObDFMParseCtx *ctx,
                              const int64_t expected_len,
                              int64_t *value_len,
                              int32_t *result,
                              int32_t value_sign)
{
  //only unsigned int
  int ret = 0;
  int32_t temp_value = 0;
  int64_t real_data_len = 0;
  int64_t date_max_len = 0;


  if (!(ctx->cur_ch_ != NULL && ctx->remain_len_ > 0) || (expected_len < 0)
      || (value_sign != -1 && value_sign != 1)) {
    ret = 1;
  } else if (!isdigit(ctx->cur_ch_[0])) {  //check the first char
    ret = 1; //ORA-01858: a non-numeric character was found where a numeric was expected
  }

  date_max_len = MIN(ctx->remain_len_, expected_len);

  while (0 == ret
         && real_data_len < date_max_len
         && isdigit(ctx->cur_ch_[real_data_len])) {
    int32_t cur_digit = (int32_t)(ctx->cur_ch_[real_data_len] - '0');

    if (temp_value * 10LL > INT32_MAX - cur_digit) {
      ret = 1;
    } else {
      temp_value = temp_value * 10 + cur_digit;
      ++real_data_len;
    }
  }

  if (0 == ret) {
    if (0 != (ret = check_int_value_length(ctx, expected_len, real_data_len))) {
    } else {
      *value_len = real_data_len;
      *result = temp_value * value_sign;
    }
  }

  return (ret);
}

int match_int_value(struct ObDFMParseCtx *ctx,
                    const int64_t expected_len,
                    int64_t *value_len,
                    int32_t *result)
{
  return match_int_value_with_sign(ctx, expected_len, value_len, result, 1);
}


int match_chars_until_space(struct ObDFMParseCtx *ctx, char **result, int64_t *result_len, int64_t *value_len)
{
  int ret = 0;
  int32_t str_len = 0;


  if (0 == ctx->remain_len_) {
    ret = 1;
  }
  while (0 == ret && str_len < ctx->remain_len_ && !isspace(ctx->cur_ch_[str_len])) {
    if (str_len >= *value_len) {
      ret = 1;
    } else {
      ++str_len;
    }
  }
  if (0 == ret) {
    *result = (char *)ctx->cur_ch_;
    *result_len = str_len;
    *value_len = str_len;
  }

  return (ret);
}

int32_t ob_time_to_date(struct ObTime *ob_time)
{
  int32_t value = 0;
  if (ZERO_DATE == ob_time->parts_[DT_DATE] && !HAS_TYPE_ORACLE(ob_time->mode_)) {
    value = ZERO_DATE;
  } else {
    int32_t days_of_years;
    int32_t leap_year_count;
    int32_t *parts = ob_time->parts_;
    parts[DT_YDAY] = DAYS_UNTIL_MON[IS_LEAP_YEAR(parts[DT_YEAR])][parts[DT_MON] - 1] + parts[DT_MDAY];
    days_of_years = (parts[DT_YEAR] - EPOCH_YEAR4) * DAYS_PER_NYEAR;
    leap_year_count = LEAP_YEAR_COUNT(parts[DT_YEAR] - 1) - LEAP_YEAR_COUNT(EPOCH_YEAR4 - 1);
    value = (int32_t)(days_of_years + leap_year_count + parts[DT_YDAY] - 1);
    parts[DT_WDAY] = WDAY_OFFSET[value % DAYS_PER_WEEK ][EPOCH_WDAY];
  }
  return value;
}

static inline my_bool match_pattern_ignore_case(struct ObDFMParseCtx *ctx, const struct ObTimeConstStr *pattern)
{
  my_bool ret_bool = FALSE;
  if (ctx->remain_len_ >= pattern->len_) {
    ret_bool = (0 == strncasecmp(ctx->cur_ch_, pattern->ptr_, pattern->len_));
  } else {
    //false
  }
  return ret_bool;
}

static inline my_bool elem_has_meridian_indicator(OB_BITMAP *flag_bitmap)
{
  return  ob_bitmap_is_set(flag_bitmap, AM) ||  ob_bitmap_is_set(flag_bitmap, PM)
         ||  ob_bitmap_is_set(flag_bitmap, AM2) ||  ob_bitmap_is_set(flag_bitmap, PM2);
}

int64_t skip_separate_chars_with_limit(struct ObDFMParseCtx *ctx, const int64_t limit, const int64_t except_char)
{
  int64_t sep_len = 0;
  while (sep_len < ctx->remain_len_ && sep_len < limit
         && is_split_char(ctx->cur_ch_[sep_len])
         && (int64_t)(ctx->cur_ch_[sep_len]) != except_char) {
    sep_len++;
  }
  ctx->cur_ch_ += sep_len;
  ctx->remain_len_ -= sep_len;
  return sep_len;
}

int64_t skip_separate_chars(struct ObDFMParseCtx *ctx)
{
  return skip_separate_chars_with_limit(ctx, MAX_VARCHAR_LENGTH, INT64_MAX);
}

int64_t skip_blank_chars(struct ObDFMParseCtx *ctx)
{
  int64_t blank_char_len = 0;
  while (blank_char_len < ctx->remain_len_
         && ' ' == ctx->cur_ch_[blank_char_len]) {
    blank_char_len++;
  }
  ctx->cur_ch_ += blank_char_len;
  ctx->remain_len_ -= blank_char_len;
  return blank_char_len;
}

const char *find_first_separator(struct ObDFMParseCtx *ctx)
{
  const char *result = NULL;
  int64_t i = 0;
  for (; NULL == result && i < ctx->remain_len_; i++) {
    if (is_split_char(ctx->cur_ch_[i])) {
      result = ctx->cur_ch_ + i;
    }
  }
  return result;
}

static int check_semantic(const DYNAMIC_ARRAY *dfm_elements, OB_BITMAP *flag_bitmap, uint64_t mode)
{
  int ret = 0;
  int64_t i = 0;
  int64_t conflict_group_bitset = 0;

  ob_bitmap_clear_all(flag_bitmap);

  for (; 0 == ret && i < dfm_elements->elements; ++i) {
    struct ObDFMElem *elem = dynamic_element(dfm_elements, i, struct ObDFMElem *);
    int64_t flag = elem->elem_flag_;
    if (!(flag > INVALID_FLAG && flag < MAX_FLAG_NUMBER)) {
      ret = 1;
    }
    //The following datetime format elements can be used in timestamp and interval format models,
    //but not in the original DATE format model: FF, TZD, TZH, TZM, and TZR
    if (0 == ret) {
      if (flag >= FF1 && flag <= FF && !HAS_TYPE_ORACLE(mode)) {
        ret = 1;
      } else if (!HAS_TYPE_TIMEZONE(mode) &&
                 (TZD == flag || TZR ==flag
                  || TZH == flag || TZM == flag)) {
        ret = 1;
      }
    }

    //check no duplicate elem first
    if (0 == ret) {
      if ( ob_bitmap_is_set(flag_bitmap, flag)) {
        ret = 1; //ORA-01810: format code appears twice
      } else {
         ob_bitmap_set_bit(flag_bitmap, flag);
      }
    }
    //check conflict in group which the element belongs to
    if (0 == ret) {
      int64_t conf_group = CONFLICT_GROUP_MAP[flag];
      if (conf_group >= 0) {
        if (0 != (conflict_group_bitset & (1 << conf_group))) {
          ret = 1;
        } else {
          conflict_group_bitset |= (1 << conf_group);
        }
      }
    }//end if
  }//end for

  if (0 == ret) {
    if ( ob_bitmap_is_set(flag_bitmap, TZM)
        && ! ob_bitmap_is_set(flag_bitmap, TZH)) {
      ret = 1;
    }
  }

  return (ret);
}

/* search matched pattern */
int parse_one_elem(struct ObDFMParseCtx *ctx, struct ObDFMElem *elem)
{
  int ret = 0;

  if (!(ctx->cur_ch_ != NULL && ctx->remain_len_ > 0)) {
    ret = 1;
  } else {
    int64_t winner_flag = INVALID_FLAG;
    int64_t max_matched_len = 0;
    int64_t flag = 0;
    for (;flag < MAX_FLAG_NUMBER; ++flag) {
      const struct ObTimeConstStr *pattern = &PATTERNEX[flag];
      if (max_matched_len < pattern->len_ && match_pattern_ignore_case(ctx, pattern)) {
        winner_flag = flag;
        max_matched_len = pattern->len_;
      }
    }

    //uppercase adjust
    if (0 == ret) {
      if (winner_flag != INVALID_FLAG) {
        elem->elem_flag_ = winner_flag;
        elem->offset_ = ctx->cur_ch_ - ctx->fmt_str_;
        switch (winner_flag) {
          case MON:
          case MONTH:
          case DAY:
          case DY:
          case AM:
          case PM:
          case AD:
          case BC: {
            if (ctx->remain_len_ < 2) {
              ret = 1;
            } else if (isupper(ctx->cur_ch_[0]) && isupper(ctx->cur_ch_[1])) {
              elem->upper_case_mode_ = ALL_CHARACTER;
            } else if (isupper(ctx->cur_ch_[0])) {
              elem->upper_case_mode_ = ONLY_FIRST_CHARACTER;
            } else {
              elem->upper_case_mode_ = NON_CHARACTER;
            }
            break;
          }

          case AM2:
          case PM2:
          case AD2:
          case BC2: {
            if (ctx->remain_len_ < 4) {
              ret = 1;
            } else if (isupper(ctx->cur_ch_[0])) {
              elem->upper_case_mode_ = ALL_CHARACTER;
            } else {
              elem->upper_case_mode_ = NON_CHARACTER;
            }
          }
          default:
            //do nothing
            break;
        }
        ctx->cur_ch_ += max_matched_len;
        ctx->remain_len_ -= max_matched_len;
      } else {
        ret = 1;
      }
    }
  }

  return (ret);
}

int parse_datetime_format_string(const char *fmt_str, const int64_t fmt_len, DYNAMIC_ARRAY *array)
{
  int ret = 0;

  if (fmt_str == NULL || fmt_len == 0) {
    //do nothing
  } else {
    int64_t skipped_len = 0;
    struct ObDFMParseCtx parse_ctx;
    parse_ctx.fmt_str_ = fmt_str;
    parse_ctx.cur_ch_ = fmt_str;
    parse_ctx.remain_len_ = fmt_len;
    parse_ctx.expected_elem_flag_ = INVALID_FLAG;
    parse_ctx.is_matching_by_expected_len_ = FALSE;

    while ((0 == ret) && parse_ctx.remain_len_ > 0) {
      //skip separate chars
      skipped_len = skip_separate_chars(&parse_ctx);
      //parse one element from head
      if (parse_ctx.remain_len_ > 0) {
        struct ObDFMElem value_elem;
        value_elem.elem_flag_ = INVALID_FLAG;
        value_elem.offset_ = -1;
        value_elem.is_single_dot_before_ = FALSE;
        value_elem.upper_case_mode_ = NON_CHARACTER;

        if((int64_t)(parse_ctx.cur_ch_ - parse_ctx.fmt_str_) > 0) {
          value_elem.is_single_dot_before_ = (skipped_len == 1 && '.' == parse_ctx.cur_ch_[-1]);
        }

        if (0 != (ret = (parse_one_elem(&parse_ctx, &value_elem)))) {
        } else if (ma_insert_dynamic(array, &value_elem)) {
          ret = 1;
        }
      }
    }
  }

  return (ret);
}

int special_mode_sprintf(char *buf, const int64_t buf_len, int64_t *pos,
                         const struct ObTimeConstStr *str, const enum UpperCaseMode mode, int64_t padding)
{
  int ret = 0;

  if (str->len_ <= 0 || NULL == str->ptr_ || NULL == buf
      || (padding > 0 && padding < str->len_)) {
    ret = 1;
  } else if (*pos + (padding > 0 ? padding : str->len_) >= buf_len) {
    ret = 1;
  } else {
    int64_t i = 0;
    for (i = 0; 0 == ret && i < str->len_; ++i) {
      char cur_char = str->ptr_[i];
      if ((cur_char >= 'a' && cur_char <= 'z')
          || (cur_char >= 'A' && cur_char <= 'Z')) {
        switch (mode) {
          case ALL_CHARACTER: {
            buf[(*pos)++] = (char)(toupper(cur_char));
            break;
          }
          case ONLY_FIRST_CHARACTER: {
            if (i == 0) {
              buf[(*pos)++] = (char)(toupper(cur_char));
            } else {
              buf[(*pos)++] = (char)(tolower(cur_char));
            }
            break;
          }
          case NON_CHARACTER: {
            buf[(*pos)++] = (char)(tolower(cur_char));
            break;
          }
          default: {
            ret = 1;
            break;
          }
        }
      } else {
        buf[(*pos)++] = cur_char;
      }//end if
    }//end for

    for (i = str->len_; i < padding; ++i) {
      buf[(*pos)++] = ' ';
    }
  }

  return (ret);
}

static const char DIGITS[] =
  "0001020304050607080910111213141516171819"
  "2021222324252627282930313233343536373839"
  "4041424344454647484950515253545556575859"
  "6061626364656667686970717273747576777879"
  "8081828384858687888990919293949596979899";

char *format_unsigned(struct ObFastFormatInt *ffi, uint64_t value)
{
  char *ptr = ffi->buf_ + (MAX_DIGITS10_STR_SIZE - 1);
  uint32_t index = 0;
  while (value >= 100) {
    index = (uint32_t)(value % 100) << 1;
    value /= 100;
    *--ptr = DIGITS[index + 1];
    *--ptr = DIGITS[index];
  }
  if (value < 10) {
    *--ptr = (char)('0' + value);
  } else {
    index = (uint32_t)(value) << 1;
    *--ptr = DIGITS[index + 1];
    *--ptr = DIGITS[index];
  }
  return ptr;
}

static void format_signed(struct ObFastFormatInt *ffi, int64_t value)
{
  uint64_t abs_value = (uint64_t)(value);
  if (value < 0) {
    abs_value = ~abs_value + 1;
    ffi->ptr_ = format_unsigned(ffi, abs_value);
    *--(ffi->ptr_) = '-';
  } else {
    ffi->ptr_ = format_unsigned(ffi, abs_value);
  }
  ffi->len_ = ffi->buf_ - ffi->ptr_ + MAX_DIGITS10_STR_SIZE - 1;
}

int data_fmt_nd(char *buffer, int64_t buf_len, int64_t *pos, const int64_t n, int64_t target)
{
  int ret = 0;
  if (n <= 0 || target < 0 || target > 999999999) {
    ret = 1;
  } else if (n > buf_len - *pos) {
    ret = 1;
  } else {
    struct ObFastFormatInt ffi;
    memset(&ffi, 0, sizeof(ffi));
    format_signed(&ffi, target);

    if (ffi.len_ > n) {
      ret = 1;
    } else {
      memset(buffer + *pos, '0', n - ffi.len_);
      memcpy(buffer + *pos + n - ffi.len_, ffi.ptr_, ffi.len_);
      *pos += n;
    }
  }
  return (ret);
}

static int32_t ob_time_to_week(const struct ObTime *ob_time, ObDTMode mode)
{
  const int32_t *parts = ob_time->parts_;
  int32_t is_sun_begin = IS_SUN_BEGIN(mode);
  int32_t is_zero_begin = IS_ZERO_BEGIN(mode);
  int32_t is_ge_4_begin = IS_GE_4_BEGIN(mode);
  int32_t week = 0;
  if (parts[DT_YDAY] > DAYS_PER_NYEAR - 3 && is_ge_4_begin && !is_zero_begin) {
    // maybe the day is in week 1 of next year.
    int32_t days_cur_year = DAYS_PER_YEAR[IS_LEAP_YEAR(parts[DT_YEAR])];
    int32_t wday_next_yday1 = WDAY_OFFSET[days_cur_year - parts[DT_YDAY] + 1][parts[DT_WDAY]];
    int32_t yday_next_week1 = YDAY_WEEK1[wday_next_yday1][is_sun_begin][is_ge_4_begin];
    if (parts[DT_YDAY] >= days_cur_year + yday_next_week1) {
      week = 1;
    }
  }
  if (0 == week) {
    int32_t wday_cur_yday1 = WDAY_OFFSET[(1 - parts[DT_YDAY]) % DAYS_PER_WEEK][parts[DT_WDAY]];
    int32_t yday_cur_week1 = YDAY_WEEK1[wday_cur_yday1][is_sun_begin][is_ge_4_begin];
    if (parts[DT_YDAY] < yday_cur_week1 && !is_zero_begin) {
      // the day is in last week of prev year.
      int32_t days_prev_year = DAYS_PER_YEAR[IS_LEAP_YEAR(parts[DT_YEAR] - 1)];
      int32_t wday_prev_yday1 = WDAY_OFFSET[(1 - days_prev_year - parts[DT_YDAY]) % DAYS_PER_WEEK][parts[DT_WDAY]];
      int32_t yday_prev_week1 = YDAY_WEEK1[wday_prev_yday1][is_sun_begin][is_ge_4_begin];
      week = (days_prev_year + parts[DT_YDAY] - yday_prev_week1 + DAYS_PER_WEEK) / DAYS_PER_WEEK;
    } else {
      week = (parts[DT_YDAY] - yday_cur_week1 + DAYS_PER_WEEK) / DAYS_PER_WEEK;
    }
  }
  return week;
}

int calculate_str_oracle_dfm_length(const struct ObTime *ob_time, 
                                    const char *fmt_str, const int64_t fmt_len,
                                    int16_t scale, int64_t *len)
{
  int ret = 0;
  if (NULL == fmt_str || fmt_len <=0
      || scale > MAX_SCALE_FOR_ORACLE_TEMPORAL) {
    ret = 1;
  } else {
    int64_t last_elem_end_pos = 0;
    int64_t i = 0;
    int64_t length = 0;
    DYNAMIC_ARRAY dfm_elems;

    if (scale < 0 ) {
      scale = DEFAULT_SCALE_FOR_ORACLE_FRACTIONAL_SECONDS;
    }

    //1. parse element from format string
    if (ma_init_dynamic_array(&dfm_elems, sizeof(struct ObDFMElem), COMMON_ELEMENT_NUMBER, 0)) {
      ret = 1;
    } else if (0 != (ret = parse_datetime_format_string(fmt_str, fmt_len, &dfm_elems))) {
    }

    //2. print each element
    for (; 0 == ret && i < dfm_elems.elements; ++i) {
      struct ObDFMElem *elem = dynamic_element(&dfm_elems, i, struct ObDFMElem *);

      //element is valid
      if (!(elem->elem_flag_ > INVALID_FLAG && elem->elem_flag_ < MAX_FLAG_NUMBER && elem->offset_ >= 0)) {
        ret = 1;
      }

      //print separate chars between elements
      if (0 == ret) {
        int64_t separate_chars_len = elem->offset_ - last_elem_end_pos;
        if (separate_chars_len > 0) {
          length += separate_chars_len;
        }
        last_elem_end_pos = elem->offset_ + PATTERNEX[elem->elem_flag_].len_;
      }

      //print current elem
      if (0 == ret) {
        switch (elem->elem_flag_) {
        case AD:
        case BC: { //TODO wjh: NLS_LANGUAGE
          const struct ObTimeConstStr *target_str = ob_time->parts_[DT_YEAR] > 0 ? &PATTERNEX[AD] : &PATTERNEX[BC];
          length += target_str->len_;
          break;
        }
        case AD2:
        case BC2: { //TODO wjh: NLS_LANGUAGE
          const struct ObTimeConstStr *str = ob_time->parts_[DT_YEAR] > 0 ? &PATTERNEX[AD2] : &PATTERNEX[BC2];
          length += str->len_;
          break;
        }
        case CC: {
          length += 2;
          break;
        }
        case SCC: {
          length += 2;
          break;
        }
        case D: {
          length += 1;
          break;
        }
        case DAY: {  //TODO wjh: NLS_LANGUAGE
          length += MAX_WDAY_NAME_LENGTH;
          break;
        }
        case DD: {
          length += 2;
          break;
        }
        case DDD: {
          length += 3;
          break;
        }
        case DY: {  //TODO wjh: 1. NLS_LANGUAGE
          const struct ObTimeConstStr *day_str = &WDAY_ABBR_NAMES[ob_time->parts_[DT_WDAY]];
          length += day_str->len_;
          break;
        }
        case DS: {  //TODO wjh: 1. NLS_TERRITORY 2. NLS_LANGUAGE
          length += 2 + 1 + 2 + 1 + 1;
          break;
        }
        case DL: { //TODO wjh: 1. NLS_DATE_FORMAT 2. NLS_TERRITORY 3. NLS_LANGUAGE
          const struct ObTimeConstStr *wday_str = &WDAY_NAMES[ob_time->parts_[DT_WDAY]];
          const struct ObTimeConstStr *mon_str = &MON_NAMES[ob_time->parts_[DT_MON]];
          length += wday_str->len_ + 2 + mon_str->len_ + 1 + 2 + 2 + 1;
          break;
        }
        case FF: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else if (0 == scale) {
            //print nothing
          } else {
            length += scale;
          }
          break;
        }
        case FF1:
        case FF2:
        case FF3:
        case FF4:
        case FF5:
        case FF6:
        case FF7:
        case FF8:
        case FF9: {
          int64_t scale = elem->elem_flag_ - FF1 + 1;
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else {
            length += scale;
          }
          break;
        }
        case HH:
        case HH12: {
          length += 2;
          break;
        }
        case HH24: {
          length += 2;
          break;
        }
        case IW: {
          length += 2;
          break;
        }
        case MI: {
          length += 2;
          break;
        }
        case MM: {
          length += 2;
          break;
        }
        case MONTH: {
          length += MAX_MON_NAME_LENGTH;
          break;
        }
        case MON: {
          const struct ObTimeConstStr *mon_str = &MON_ABBR_NAMES[ob_time->parts_[DT_MON]];
          length += mon_str->len_;
          break;
        }
        case AM:
        case PM: {
          const struct ObTimeConstStr *str = ob_time->parts_[DT_HOUR] >= 12 ?
              &PATTERNEX[PM] : &PATTERNEX[AM];
          length += str->len_;
          break;
        }
        case AM2:
        case PM2: {
          const struct ObTimeConstStr *str = ob_time->parts_[DT_HOUR] >= 12 ?
                       &PATTERNEX[PM2] : &PATTERNEX[AM2];
          length += str->len_;
          break;
        }
        case Q: {
          length += 1;
          break;
        }
        case RR: {
          length += 2;
          break;
        }
        case RRRR: {
          length += 4;
          break;
        }
        case SS: {
          length += 2;
          break;
        }
        case SSSSS: {
          length += 5;
          break;
        }
        case WW: {  //the first complete week of a year
          length += 2;
          break;
        }
        case W: {  //the first complete week of a month
          length += 1;
          break;
        }
        case YGYYY: {
          length += 1 + 1 + 3;
          break;
        }
        case YEAR: {
          ret = 1;
          break;
        }
        case SYEAR: {
          ret = 1;
          break;
        }
        case SYYYY: {
          length += 5;
          break;
        }
        case YYYY:
        case IYYY: {
          length += 4;
          break;
          break;
        }
        case YYY:
        case IYY: {
          length += 3;
          break;
        }
        case YY:
        case IY: {
          length += 2;
          break;
        }
        case Y:
        case I: {
          length += 1;
          break;
        }
        case TZD: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else if (ob_time->time_zone_id_ != -1) {
            length += strlen(ob_time->tzd_abbr_);
          } else {
            //do nothing
          }
          break;
        }
        case TZR: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1; 
          } else if (ob_time->time_zone_id_ != -1) {
            length += strlen(ob_time->tz_name_);
          } else {
            length += 6;
          }
          break;
        }
        case TZH: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else {
            length += 3;
          }
          break;
        }

        case TZM: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else {
            length += 2;
          }
          break;
        }
        case X: {
          length += 1;
          break;
        }

        default: {
          ret = 1;
          break;
        }
        }
      } //end if
    }//end for

    if (0 == ret) {
      //print the rest separate chars
      int64_t separate_chars_len = fmt_len - last_elem_end_pos;
      if (separate_chars_len > 0) {
        length += separate_chars_len;
      }
    }

    ma_delete_dynamic(&dfm_elems);
    *len = length;
  }//end if

  return (ret);
}

int ob_time_to_str_oracle_dfm(const struct ObTime *ob_time,
                              const char *fmt_str, const int64_t fmt_len,
                              int16_t scale,
                              char *buf, int64_t buf_len,
                              int64_t *pos)
{
  int ret = 0;

  if (NULL == buf || buf_len <= 0
      || NULL == fmt_str || fmt_len <=0
      || scale > MAX_SCALE_FOR_ORACLE_TEMPORAL) {
    ret = 1;
  } else {
    int64_t last_elem_end_pos = 0;
    int64_t i = 0;
    DYNAMIC_ARRAY dfm_elems;

    if (scale < 0 ) {
      scale = DEFAULT_SCALE_FOR_ORACLE_FRACTIONAL_SECONDS;
    }

    //1. parse element from format string
    if (ma_init_dynamic_array(&dfm_elems, sizeof(struct ObDFMElem), COMMON_ELEMENT_NUMBER, 0)) {
      ret = 1;
    } else if (0 != (ret = parse_datetime_format_string(fmt_str, fmt_len, &dfm_elems))) {
    }

    //2. print each element
    for (; 0 == ret && i < dfm_elems.elements; ++i) {
      struct ObDFMElem *elem = dynamic_element(&dfm_elems, i, struct ObDFMElem *);

      //element is valid
      if (!(elem->elem_flag_ > INVALID_FLAG && elem->elem_flag_ < MAX_FLAG_NUMBER && elem->offset_ >= 0)) {
        ret = 1;
      }

      //print separate chars between elements
      if (0 == ret) {
        int64_t separate_chars_len = elem->offset_ - last_elem_end_pos;
        if (separate_chars_len > 0) {
          ret = databuff_printf(buf, buf_len, pos, "%.*s", (int32_t)(separate_chars_len),
                                fmt_str + last_elem_end_pos);
        }
        last_elem_end_pos = elem->offset_ + PATTERNEX[elem->elem_flag_].len_;
      }

      //print current elem
      if (0 == ret) {
        switch (elem->elem_flag_) {
        case AD:
        case BC: { //TODO wjh: NLS_LANGUAGE
          const struct ObTimeConstStr *target_str = ob_time->parts_[DT_YEAR] > 0 ? &PATTERNEX[AD] : &PATTERNEX[BC];
          ret = special_mode_sprintf(buf, buf_len, pos, target_str, elem->upper_case_mode_, -1);
          break;
        }
        case AD2:
        case BC2: { //TODO wjh: NLS_LANGUAGE
          const struct ObTimeConstStr *str = ob_time->parts_[DT_YEAR] > 0 ? &PATTERNEX[AD2] : &PATTERNEX[BC2];
          ret = special_mode_sprintf(buf, buf_len, pos, str, elem->upper_case_mode_, -1);
          break;
        }
        case CC: {
          ret = databuff_printf(buf, buf_len, pos, "%02d", (abs(ob_time->parts_[DT_YEAR]) + 99) / 100);
          break;
        }
        case SCC: {
          char symbol = ob_time->parts_[DT_YEAR] > 0 ? ' ' : '-';
          ret = databuff_printf(buf, buf_len, pos, "%c%02d", symbol, (abs(ob_time->parts_[DT_YEAR]) + 99) / 100);
          break;
        }
        case D: {
          ret = databuff_printf(buf, buf_len, pos, "%d", ob_time->parts_[DT_WDAY] % 7 + 1);
          break;
        }
        case DAY: {  //TODO wjh: NLS_LANGUAGE
          const struct ObTimeConstStr *day_str = &WDAY_NAMES[ob_time->parts_[DT_WDAY]];
          ret = special_mode_sprintf(buf, buf_len, pos, day_str, elem->upper_case_mode_, MAX_WDAY_NAME_LENGTH);
          break;
        }
        case DD: {
          ret = databuff_printf(buf, buf_len, pos, "%02d", ob_time->parts_[DT_MDAY]);
          break;
        }
        case DDD: {
          ret = databuff_printf(buf, buf_len, pos, "%03d", ob_time->parts_[DT_YDAY]);
          break;
        }
        case DY: {  //TODO wjh: 1. NLS_LANGUAGE
          const struct ObTimeConstStr *day_str = &WDAY_ABBR_NAMES[ob_time->parts_[DT_WDAY]];
          ret = special_mode_sprintf(buf, buf_len, pos, day_str, elem->upper_case_mode_, -1);
          break;
        }
        case DS: {  //TODO wjh: 1. NLS_TERRITORY 2. NLS_LANGUAGE
          ret = databuff_printf(buf, buf_len, pos, "%02d/%02d/%d",
                                ob_time->parts_[DT_MON], ob_time->parts_[DT_MDAY], ob_time->parts_[DT_YEAR]);
          break;
        }
        case DL: { //TODO wjh: 1. NLS_DATE_FORMAT 2. NLS_TERRITORY 3. NLS_LANGUAGE
          const struct ObTimeConstStr *wday_str = &WDAY_NAMES[ob_time->parts_[DT_WDAY]];
          const struct ObTimeConstStr *mon_str = &MON_NAMES[ob_time->parts_[DT_MON]];
          ret = databuff_printf(buf, buf_len, pos, "%.*s, %.*s %02d, %d",
                                wday_str->len_, wday_str->ptr_,
                                mon_str->len_, mon_str->ptr_,
                                ob_time->parts_[DT_MDAY], ob_time->parts_[DT_YEAR]);
          break;
        }
        case FF: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else if (0 == scale) {
            //print nothing
          } else {
            ret = data_fmt_nd(buf, buf_len, pos, scale,
                              ob_time->parts_[DT_USEC] / power_of_10[MAX_SCALE_FOR_ORACLE_TEMPORAL - scale]);
          }
          break;
        }
        case FF1:
        case FF2:
        case FF3:
        case FF4:
        case FF5:
        case FF6:
        case FF7:
        case FF8:
        case FF9: {
          int64_t scale = elem->elem_flag_ - FF1 + 1;
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else {
            ret = data_fmt_nd(buf, buf_len, pos, scale,
                              ob_time->parts_[DT_USEC] / power_of_10[MAX_SCALE_FOR_ORACLE_TEMPORAL - scale]);
          }
          break;
        }
        case HH:
        case HH12: {
          int32_t h = ob_time->parts_[DT_HOUR] % 12;
          if (0 == h) {
            h = 12;
          }
          ret = databuff_printf(buf, buf_len, pos, "%02d", h);
          break;
        }
        case HH24: {
          int32_t h = ob_time->parts_[DT_HOUR];
          ret = databuff_printf(buf, buf_len, pos, "%02d", h);
          break;
        }
        case IW: {
          ret = databuff_printf(buf, buf_len, pos, "%02d", ob_time_to_week(ob_time, WEEK_MODE[3]));
          break;
        }
        case J: {
          const int32_t base_julian_day = 2378497;    // julian day of 1800-01-01
          const int32_t base_date = -62091;           //ob_time.parts_[DT_DATE] of 1800-01-01
          if (ob_time->parts_[DT_DATE] < base_date) {
            ret = 1;
          } else {
            int32_t julian_day = base_julian_day + ob_time->parts_[DT_DATE] - base_date;
            ret = databuff_printf(buf, buf_len, pos, "%07d", julian_day);
          }
          break;
        }
        case MI: {
          ret = databuff_printf(buf, buf_len, pos, "%02d", ob_time->parts_[DT_MIN]);
          break;
        }
        case MM: {
          ret = databuff_printf(buf, buf_len, pos, "%02d", ob_time->parts_[DT_MON]);
          break;
        }
        case MONTH: {
          const struct ObTimeConstStr *mon_str = &MON_NAMES[ob_time->parts_[DT_MON]];
          ret = special_mode_sprintf(buf, buf_len, pos, mon_str, elem->upper_case_mode_, MAX_MON_NAME_LENGTH);
          break;
        }
        case MON: {
          const struct ObTimeConstStr *mon_str = &MON_ABBR_NAMES[ob_time->parts_[DT_MON]];
          ret = special_mode_sprintf(buf, buf_len, pos, mon_str, elem->upper_case_mode_, -1);
          break;
        }
        case AM:
        case PM: {
          const struct ObTimeConstStr *str = ob_time->parts_[DT_HOUR] >= 12 ? &PATTERNEX[PM] : &PATTERNEX[AM];
          ret = special_mode_sprintf(buf, buf_len, pos, str, elem->upper_case_mode_, -1);
          break;
        }
        case AM2:
        case PM2: {
          const struct ObTimeConstStr *str = ob_time->parts_[DT_HOUR] >= 12 ? &PATTERNEX[PM2] : &PATTERNEX[AM2];
          ret = special_mode_sprintf(buf, buf_len, pos, str, elem->upper_case_mode_, -1);
          break;
        }
        case Q: {
          ret = databuff_printf(buf, buf_len, pos, "%d", (ob_time->parts_[DT_MON] + 2) / 3);
          break;
        }
        case RR: {
          ret = databuff_printf(buf, buf_len, pos, "%02d", (ob_time->parts_[DT_YEAR]) % 100);
          break;
        }
        case RRRR: {
          ret = databuff_printf(buf, buf_len, pos, "%04d", ob_time->parts_[DT_YEAR]);
          break;
        }
        case SS: {
          ret = databuff_printf(buf, buf_len, pos, "%02d", ob_time->parts_[DT_SEC]);
          break;
        }
        case SSSSS: {
          ret = databuff_printf(buf, buf_len, pos, "%05d",
                                ob_time->parts_[DT_HOUR] * 3600 + ob_time->parts_[DT_MIN] * 60 + ob_time->parts_[DT_SEC]);
          break;
        }
        case WW: {  //the first complete week of a year
          ret = databuff_printf(buf, buf_len, pos, "%02d", (ob_time->parts_[DT_YDAY] - 1) / 7 + 1);
          break;
        }
        case W: {  //the first complete week of a month
          ret = databuff_printf(buf, buf_len, pos, "%d", (ob_time->parts_[DT_MDAY] - 1) / 7 + 1);
          break;
        }
        case YGYYY: {
          ret = databuff_printf(buf, buf_len, pos, "%d,%03d",
                                abs(ob_time->parts_[DT_YEAR]) / 1000, abs(ob_time->parts_[DT_YEAR]) % 1000);
          break;
        }
        case YEAR: {
          ret = 1;
          break;
        }
        case SYEAR: {
          ret = 1;
          break;
        }
        case SYYYY: {
          const char* fmt_str = ob_time->parts_[DT_YEAR] < 0 ? "-%04d" : " %04d";
          ret = databuff_printf(buf, buf_len, pos, fmt_str, abs(ob_time->parts_[DT_YEAR]));
          break;
        }
        case YYYY:
        case IYYY: {
          ret = databuff_printf(buf, buf_len, pos, "%04d", abs(ob_time->parts_[DT_YEAR]));
          break;
        }
        case YYY:
        case IYY: {
          ret = databuff_printf(buf, buf_len, pos, "%03d", abs(ob_time->parts_[DT_YEAR] % 1000));
          break;
        }
        case YY:
        case IY: {
          ret = databuff_printf(buf, buf_len, pos, "%02d", abs(ob_time->parts_[DT_YEAR] % 100));
          break;
        }
        case Y:
        case I: {
          ret = databuff_printf(buf, buf_len, pos, "%01d", abs(ob_time->parts_[DT_YEAR] % 10));
          break;
        }
        case TZD: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else if (ob_time->time_zone_id_ != -1) {
            ret = databuff_printf(buf, buf_len, pos, "%.*s", (int)strlen(ob_time->tzd_abbr_), ob_time->tzd_abbr_);
          } else {
            //do nothing
          }
          break;
        }
        case TZR: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else if (ob_time->time_zone_id_ != -1) {
            ret = databuff_printf(buf, buf_len, pos, "%.*s", (int)strlen(ob_time->tz_name_), ob_time->tz_name_);
          } else {
            const char* fmt_str = ob_time->parts_[DT_OFFSET_MIN] < 0 ? "-%02d:%02d" : "+%02d:%02d";
            ret = databuff_printf(buf, buf_len, pos, fmt_str,
                    abs(ob_time->parts_[DT_OFFSET_MIN]) / 60, abs(ob_time->parts_[DT_OFFSET_MIN]) % 60);
          }
          break;
        }
        case TZH: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else {
            const char* fmt_str = ob_time->parts_[DT_OFFSET_MIN] < 0 ? "-%02d" : "+%02d";
            ret = databuff_printf(buf, buf_len, pos, fmt_str, abs(ob_time->parts_[DT_OFFSET_MIN]) / 60);
          }
          break;
        }
        case TZM: {
          if (!HAS_TYPE_ORACLE(ob_time->mode_)) {
            ret = 1;
          } else {
            ret = databuff_printf(buf, buf_len, pos, "%02d", abs(ob_time->parts_[DT_OFFSET_MIN]) % 60);
          }
          break;
        }
        case X: {
          ret = databuff_printf(buf, buf_len, pos, ".");
          break;
        }
        default: {
          ret = 1;
          break;
        }
        } //end switch
        if (0 != ret) {
        }
      } //end if
    }//end for

    if (0 == ret) {
      //print the rest separate chars
      int64_t separate_chars_len = fmt_len - last_elem_end_pos;
      if (separate_chars_len > 0) {
        if (0 != (ret = databuff_printf(buf, buf_len, pos, "%.*s", (int32_t)(separate_chars_len),
                                        fmt_str + last_elem_end_pos))) {
        }
      }
    }

    ma_delete_dynamic(&dfm_elems);
  }//end if

  return (ret);
}

//TODO be delete, not to use
/**
 * @brief convert string to ob_time struct according to oracle datetime format model
 * @param in:   str         input string
 * @param in:   format      format string
 * @param out:  ob_time     memory struct of datetime
 * @param out:  scale       scale of fractional seconds
 */
int str_to_ob_time_oracle_dfm(const char *str, const int64_t str_len,
                              const char *fmt_str, const int64_t fmt_len,
                              struct ObTime *ob_time,
                              int16_t scale)
{
  int ret = 0;

  if (NULL == str || str_len <= 0
      || NULL == fmt_str || fmt_len <=0
      || scale > MAX_SCALE_FOR_ORACLE_TEMPORAL) {
    ret = 1;
  } else {
    memset(ob_time, 0, sizeof(struct ObTime));
    ob_time->mode_ |= DT_TYPE_DATETIME;
    ob_time->mode_ |= DT_TYPE_ORACLE;
    ob_time->mode_ |= DT_TYPE_TIMEZONE;
  }

  if (0 == ret) {
    DYNAMIC_ARRAY dfm_elems;
    OB_BITMAP elem_flags = {.bitmap = NULL, .n_bits = 0};
    int32_t temp_tzh_value = -1;   //positive value is legal
    int32_t temp_tzm_value = -1;   //positive value is legal
    int32_t temp_tz_factor = 0;

    int32_t tz_hour = 0;  //will be negetive when time zone offset < 0
    int32_t tz_min = 0;   //will be negetive when time zone offset < 0

    //1. parse element from format string
    if (ma_init_dynamic_array(&dfm_elems, sizeof(struct ObDFMElem), COMMON_ELEMENT_NUMBER, 0)) {
      ret = 1;
    } else if (ob_bitmap_init(&elem_flags, MAX_FLAG_NUMBER)) {
      ret = 1;
    } else if (0 != (ret = parse_datetime_format_string(fmt_str, fmt_len, &dfm_elems))) {
    } else if (0 != (ret = check_semantic(&dfm_elems, &elem_flags, ob_time->mode_))) {
    } else {
      //3. go through each element, set and check conflict for ob_time parts: Year, Month, Day, Hour, Minute, Second
      int64_t last_elem_end_pos = 0;
      int64_t conflict_part_bitset = 0;
      int64_t elem_idx = 0;
      int64_t input_sep_len = 0;
      int64_t part_blank1_len = 0;
      int64_t part_sep_len = 0;
      int64_t part_blank2_len = 0;
      int64_t format_sep_len = 0;
      //static_assert((1 << TOTAL_PART_CNT) < INT64_MAX, "for time_part_conflict_bitset");
      int32_t yday_temp_value = ZERO_DATE; //as invalid value
      int32_t wday_temp_value = ZERO_DATE; //as invalid value
      int32_t date_temp_value = ZERO_DATE; //as invalid value
      int32_t hh12_temp_value = ZERO_TIME;
      int32_t julian_year_value = ZERO_DATE;  //as invalid value
      my_bool is_after_noon = FALSE;
      my_bool is_before_christ = FALSE;
      my_bool has_digit_tz_in_TZR = FALSE;
      int64_t first_non_space_sep_char = INT64_MAX;
      int64_t ignore_fs_flag = FALSE;

      struct ObDFMParseCtx ctx;
      ctx.fmt_str_ = str;
      ctx.cur_ch_ = str;
      ctx.remain_len_ = str_len;
      ctx.expected_elem_flag_ = INVALID_FLAG;
      ctx.is_matching_by_expected_len_ = FALSE;

      for (elem_idx = 0; 0 == ret && elem_idx < dfm_elems.elements; ++elem_idx) {
        struct ObDFMElem *elem = dynamic_element(&dfm_elems, elem_idx, struct ObDFMElem *);

        //element is valid
        if (!(elem->elem_flag_ > INVALID_FLAG && elem->elem_flag_ < MAX_FLAG_NUMBER && elem->offset_ >= 0)) {
          ret = 1;
        } else {
        }

        //1. check separate chars and skip blank chars first
        if (0 == ret) {
          //get format sep chars len
          format_sep_len = elem->offset_ - last_elem_end_pos;
          last_elem_end_pos = elem->offset_ + PATTERNEX[elem->elem_flag_].len_;
          //parse input string and skip them
          part_blank1_len = skip_blank_chars(&ctx);
          //The # of skipped non-blank chars is according to format_str
          first_non_space_sep_char = (0 == ctx.remain_len_) ? INT64_MAX : ctx.cur_ch_[0];
          if (X == elem->elem_flag_) {
            /* 特殊情况, 不考虑X前面的非空白分隔符 */
            part_sep_len = 0;
          } else {
            part_sep_len = skip_separate_chars_with_limit(&ctx, format_sep_len, INT64_MAX);
          }
          part_blank2_len = skip_blank_chars(&ctx);
          input_sep_len = part_blank1_len + part_sep_len + part_blank2_len;
        }

        if (0 == ret && (0 == ctx.remain_len_)) {
          break;   //if all the input chars has beeen processed, break this loop
        }

        //2. next, parse the current element
        if (0 == ret) {
          int64_t parsed_elem_len = 0;
          const int64_t expected_elem_len = EXPECTED_MATCHING_LENGTH[elem->elem_flag_];
          ctx.expected_elem_flag_ = elem->elem_flag_;
          ctx.is_matching_by_expected_len_ = (format_sep_len > 0 && input_sep_len == 0);

          switch (elem->elem_flag_) {
            case AD:
            case BC:
            case AD2:
            case BC2: { //TODO wjh: NLS_LANGUAGE
              my_bool is_with_dot = (AD2 == elem->elem_flag_|| BC2 == elem->elem_flag_);
              my_bool is_ad = FALSE;
              my_bool is_bc = FALSE;
              parsed_elem_len = is_with_dot ? PATTERNEX[AD2].len_ : PATTERNEX[AD].len_;
              is_ad = match_pattern_ignore_case(&ctx, is_with_dot ?
                             &PATTERNEX[AD2] : &PATTERNEX[AD]);
              is_bc = match_pattern_ignore_case(&ctx, is_with_dot ?
                             &PATTERNEX[BC2] : &PATTERNEX[BC]);
              if (!is_ad && !is_bc) {
                ret = 1;
              } else {
                is_before_christ = is_bc;
              }
              break;
            }
            case D: {
              int32_t wday = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &wday))) {
              } else if (0 != (ret = check_validate(&LIMITER_WEEK_DAY, wday))) {
              } else {
                //oracle numbered sunday as 1 in territory of CHINA
                //TODO wjh: hard code for now, need look up NLS_TERRITORIES
                wday_temp_value = (wday + 5) % 7 + 1;
              }
              break;
            }
            case DY:
            case DAY: {  //TODO wjh: NLS_LANGUAGE NLS_TERRITORIES
              int32_t wday = 0;
              for (wday = 1; wday <= DAYS_PER_WEEK; ++wday) {
                const struct ObTimeConstStr *day_str = (elem->elem_flag_ == DAY) ? &WDAY_NAMES[wday] : &WDAY_ABBR_NAMES[wday];
                if (match_pattern_ignore_case(&ctx, day_str)) {
                  parsed_elem_len = day_str->len_;
                  break;
                }
              }
              if (0 != (ret = check_validate(&LIMITER_WEEK_DAY, wday))) {
              } else {
                wday_temp_value = wday;
              }
              break;
            }
            case DD: {
              int32_t mday = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &mday))) {
              } else if (0 != (ret = check_validate(&LIMITER_MONTH_DAY, mday))) {
              } else {
                //may conflict with DDD
                ret = set_ob_time_part_directly(ob_time, &conflict_part_bitset, DT_MDAY, mday);
              }
              break;
            }
            case DDD: {
              int32_t yday = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &yday))) {
              } else if (0 != (ret = check_validate(&LIMITER_YEAR_DAY, yday))) {
              } else {
                yday_temp_value = yday;
              }
              break;
            }
            case DS: {  //TODO wjh: impl it NEED NLS_DATE_FORMAT NLS_TERRITORY NLS_LANGUAGE
              ret = 1;
              break;
            }
            case DL: { //TODO wjh: impl it NEED NLS_DATE_FORMAT NLS_TERRITORY NLS_LANGUAGE
              ret = 1;
              break;
            }
            case FF:
            case FF1:
            case FF2:
            case FF3:
            case FF4:
            case FF5:
            case FF6:
            case FF7:
            case FF8:
            case FF9: {
              int32_t usec = 0;
              //format string has '.' or 'X', but input string does not contain '.'
              //do nothing, skip element FF and revert ctx by the length of parsed chars
              if (ignore_fs_flag) {
                ctx.cur_ch_ -= (part_blank1_len + part_sep_len + part_blank2_len);
                ctx.remain_len_ += (part_blank1_len + part_sep_len + part_blank2_len);
              } else if (elem->is_single_dot_before_ && '.' != first_non_space_sep_char) {
                ctx.cur_ch_ -= (part_sep_len + part_blank2_len);
                ctx.remain_len_ += (part_sep_len + part_blank2_len);
              } else if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &usec))) {
              } else {
                scale = (int16_t)(parsed_elem_len);
                usec = (int32_t)(usec * power_of_10[MAX_SCALE_FOR_ORACLE_TEMPORAL - parsed_elem_len]);
                ob_time->parts_[DT_USEC] = usec;
              }
              break;
            }

            case TZH:
            case TZM: {
              if ( ob_bitmap_is_set(&elem_flags, TZR) ||  ob_bitmap_is_set(&elem_flags, TZD)) {
                ret = 1;
              } else {
                int32_t value = 0;
                int32_t local_tz_factor = 1;
                /*
                 * SQL> alter session set NLS_TIMESTAMP_TZ_FORMAT='DD-MON-RR HH.MI.SS AM TZH:TZM';
                 * SQL> alter session set time_zone='Asia/Shanghai';
                 * SQL> select cast('01-SEP-20 11.11.11' as timestamp with time zone) from dual;
                 * 01-SEP-20 11.11.11 AM +08:00
                 */
                ob_time->is_tz_name_valid_ = FALSE;
                if (TZH == elem->elem_flag_) {
                  if (is_sign_char(ctx.cur_ch_[0])) {
                    local_tz_factor = ('-' == ctx.cur_ch_[0] ? -1 : 1);
                    ctx.cur_ch_ += 1;
                    ctx.remain_len_ -= 1;
                  } else {
                    if (((int64_t)(ctx.cur_ch_ - ctx.fmt_str_)) > 0 && input_sep_len > format_sep_len) {
                      //if the input valid separate chars > format separate chars
                      //the superfluous '-' will be regarded as minus sign
                      local_tz_factor = ((int64_t)('-') == ctx.cur_ch_[-1] ? -1 : 1);
                    }
                  }
                  temp_tz_factor = local_tz_factor;  //1 or -1, but never be 0
                }

                if (0 == ctx.remain_len_) {
                  //do nothing
                } else if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &value))) {
                } else if (0 != (ret = (elem->elem_flag_ == TZH ? check_validate(&LIMITER_TIMEZONE_HOUR_ABS, value) : check_validate(&LIMITER_TIMEZONE_MIN_ABS, value)))) {
                } else {
                  if (elem->elem_flag_ == TZH) {
                    temp_tzh_value = value;
                  } else {
                    temp_tzm_value = value;
                  }
                }
              }
              break;
            }

            case TZR: {
              if ( ob_bitmap_is_set(&elem_flags, TZH) ||  ob_bitmap_is_set(&elem_flags, TZM)) {
                ret = 1;
              } else {
                int32_t local_tz_factor = 1;
                if (isdigit(ctx.cur_ch_[0]) || is_sign_char(ctx.cur_ch_[0])) {  //case1: digits
                  int32_t tmp_tz_hour = 0;
                  int32_t tmp_tz_min = 0;
                  char *digits_timezone;
                  int64_t digits_timezone_len;
                  if (is_sign_char(ctx.cur_ch_[0])) {
                    local_tz_factor = ('-' == ctx.cur_ch_[0] ? -1 : 1);
                    ctx.cur_ch_ += 1;
                    ctx.remain_len_ -= 1;
                  } else if (part_blank1_len + part_sep_len > format_sep_len
                             && is_sign_char(ctx.cur_ch_[-1])) {
                    local_tz_factor = ('-' == ctx.cur_ch_[-1] ? -1 : 1);
                  }

                  parsed_elem_len = UNKNOWN_LENGTH_OF_ELEMENT;
                  if (!(0 == ctx.remain_len_)) {
                    ob_time->is_tz_name_valid_ = FALSE;
                  }
                  if (0 == ctx.remain_len_) {
                    //do nothing
                  } else if (0 != (ret = match_chars_until_space(&ctx, &digits_timezone, &digits_timezone_len, &parsed_elem_len))) {
                    //do nothing
                  } else {
                    const char *local_sep = NULL;
                    struct ObDFMParseCtx local_ctx;
                    local_ctx.fmt_str_ = digits_timezone;
                    local_ctx.cur_ch_ = digits_timezone;
                    local_ctx.remain_len_ = digits_timezone_len;
                    local_ctx.expected_elem_flag_ = INVALID_FLAG;
                    local_ctx.is_matching_by_expected_len_ = FALSE;

                    local_sep = find_first_separator(&local_ctx);

                    if (NULL == local_sep) {
                    } else {
                      int64_t hour_expected_len = local_sep - digits_timezone;
                      int64_t local_parsed_len = 0;
                      if (0 != (ret = match_int_value_with_sign(&local_ctx, hour_expected_len, &local_parsed_len,
                                                                &tmp_tz_hour, local_tz_factor))) {
                      } else if (0 != (ret = check_validate(&LIMITER_TIMEZONE_HOUR_ABS, tmp_tz_hour))) {
                      } else if (hour_expected_len != local_parsed_len) {
                        ret = 1; //invalid time zone
                      } else {
                        local_ctx.cur_ch_ += hour_expected_len + 1;
                        local_ctx.remain_len_ -= hour_expected_len + 1;
                        if (0 == local_ctx.remain_len_) {
                          has_digit_tz_in_TZR = TRUE;
                          tz_hour = tmp_tz_hour;
                          tz_min = tmp_tz_min;
                        } else if (0 != (ret = match_int_value_with_sign(&local_ctx, parsed_elem_len - hour_expected_len - 1,
                                                                         &local_parsed_len, &tmp_tz_min, local_tz_factor))) {
                        } else if (0 != (ret = check_validate(&LIMITER_TIMEZONE_MIN_ABS, tmp_tz_min))) {
                        } else if (parsed_elem_len != hour_expected_len + local_parsed_len + 1) {
                          ret = 1; //invalid time zone
                        } else {
                          has_digit_tz_in_TZR = TRUE;
                          tz_hour = tmp_tz_hour;
                          tz_min = tmp_tz_min;
                        }
                      }
                    }
                  }
                } else { //case2: strings
                  char *tzr_str;
                  int64_t tzr_str_len;
                  parsed_elem_len = OB_MAX_TZ_NAME_LEN - 1;
                  if (0 != (ret = match_chars_until_space(&ctx, &tzr_str, &tzr_str_len, &parsed_elem_len))) {
                  } else {
                    memcpy(ob_time->tz_name_, tzr_str, tzr_str_len);
                    ob_time->tz_name_[tzr_str_len] = '\0';
                    ob_time->is_tz_name_valid_ = TRUE;
                  }
                }
              }
              break;
            }

            case TZD: {
              if ( ob_bitmap_is_set(&elem_flags, TZH) ||  ob_bitmap_is_set(&elem_flags, TZM)) {
                ret = 1;
              } else if (has_digit_tz_in_TZR) {
                ret = 1;
              } else {
                char *tzd_str;
                int64_t tzd_str_len;
                parsed_elem_len = OB_MAX_TZ_ABBR_LEN - 1;
                if (0 != (ret = match_chars_until_space(&ctx, &tzd_str, &tzd_str_len, &parsed_elem_len))) {
                } else {
                  memcpy(ob_time->tzd_abbr_, tzd_str, tzd_str_len);
                  ob_time->tzd_abbr_[tzd_str_len] = '\0';
                }
              }
              break;
            }

            case J: {
              const int32_t base_julian_day = 2378497;    // julian day of 1800-01-01
              const int32_t base_date = -62091;           //ob_time.parts_[DT_DATE] of 1800-01-01
              int32_t julian_day = 0;
              int32_t ob_time_date = 0;
              struct ObTime tmp_ob_time;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len,
                                              &parsed_elem_len, &julian_day))) {
              } else if (0 != (ret = check_validate(&LIMITER_JULIAN_DATE, julian_day))) {
              } else if (julian_day < base_julian_day) {
                ret = 1;
              } else {
                ob_time_date = julian_day - base_julian_day + base_date;
                if (0 != (ret = date_to_ob_time(ob_time_date, &tmp_ob_time))) {
                } else if (0 != (ret = set_ob_time_year_may_conflict(ob_time, &julian_year_value,
                                                                     tmp_ob_time.parts_[DT_YEAR],
                                                                     tmp_ob_time.parts_[DT_YEAR],
                                                                     TRUE /* overwrite */))) {
                } else {
                  yday_temp_value = tmp_ob_time.parts_[DT_YDAY];
                }
              }
              break;
            }

            case HH:
            case HH12: {
              int32_t hour = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &hour))) {
              } else if (0 != (ret = check_validate(&LIMITER_HOUR12, hour))) {
              } else if (!elem_has_meridian_indicator(&elem_flags)) {
                ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_HOUR, hour);
              } else {
                hh12_temp_value = hour;
              }
              break;
            }
            case HH24: {
              int32_t hour = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &hour))) {
              } else if (0 != (ret = check_validate(&LIMITER_HOUR24, hour))) {
              } else if (elem_has_meridian_indicator(&elem_flags)) {
                ret = 1;
              } else {
                //may conflict with SSSSS
                ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_HOUR, hour);
              }
              break;
            }
            case MI: {
              int32_t min = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &min))) {
              } else if (0 != (ret = check_validate(&LIMITER_MINUTE, min))) {
              } else {
                //may conflict with SSSSS
                ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_MIN, min);
              }
              break;
            }
            case MM: {
              int32_t mon = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &mon))) {
              } else if (0 != (ret = check_validate(&LIMITER_MONTH_DAY, mon))) {
              } else {
                //may conflict with DDD
                ret = set_ob_time_part_directly(ob_time, &conflict_part_bitset, DT_MON, mon);
              }
              break;
            }
            case MON:
            case MONTH: {
              int32_t mon = 0;
              for (mon = LIMITER_MONTH.MIN; mon <= LIMITER_MONTH.MAX; ++mon) {
                const struct ObTimeConstStr *mon_str = (elem->elem_flag_ == MONTH) ? &MON_NAMES[mon] : &MON_ABBR_NAMES[mon];
                if (match_pattern_ignore_case(&ctx, mon_str)) {
                  parsed_elem_len = mon_str->len_;
                  break;
                }
              }
              if (0 != (ret = check_validate(&LIMITER_MONTH, mon))) {
              } else {
                //may conflict with DDD
                ret = set_ob_time_part_directly(ob_time, &conflict_part_bitset, DT_MON, mon);
              }
              break;
            }
            case AM:
            case PM:
            case AM2:
            case PM2: {
              my_bool is_with_dot = (AM2 == elem->elem_flag_|| PM2 == elem->elem_flag_);
              my_bool is_am = FALSE;
              my_bool is_pm = FALSE;
              parsed_elem_len = is_with_dot ? PATTERNEX[AM2].len_ : PATTERNEX[AM].len_;
              is_am = match_pattern_ignore_case(&ctx,
                             is_with_dot ? &PATTERNEX[AM2] : &PATTERNEX[AM]);
              is_pm = match_pattern_ignore_case(&ctx,
                             is_with_dot ? &PATTERNEX[PM2] : &PATTERNEX[PM]);
              if (!is_am && !is_pm) {
                ret = 1;
              } else {
                is_after_noon = is_pm;
              }
              break;
            }
            case RR:
            case RRRR: {
              int32_t round_year = 0;
              int32_t conflict_check_year = 0;
              if (0 != (ret = match_int_value(&ctx, 4, &parsed_elem_len, &round_year))) {
              } else if (parsed_elem_len > 2) {
                conflict_check_year = round_year;
                //do nothing
              } else {
                int32_t first_two_digits_of_current_year = (ob_time->parts_[DT_YEAR] / 100) % 100;
                int32_t last_two_digits_of_current_year = ob_time->parts_[DT_YEAR] % 100;
                conflict_check_year = round_year;
                if (round_year < 50) { //0~49
                  if (last_two_digits_of_current_year < 50) {
                    round_year += first_two_digits_of_current_year * 100;
                  } else {
                    round_year += (first_two_digits_of_current_year + 1) * 100;
                  }
                } else if (round_year < 100) { //50~99
                  if (last_two_digits_of_current_year < 50) {
                    round_year += (first_two_digits_of_current_year - 1) * 100;
                  } else {
                    round_year += first_two_digits_of_current_year * 100;
                  }
                }
              }
              if (0 == ret) {
                if (0 != (ret = check_validate(&LIMITER_YEAR, round_year))) { //TODO wjh: negetive year number
                } else if (0 != (ret = set_ob_time_year_may_conflict(ob_time, &julian_year_value,
                                                                     conflict_check_year, round_year,
                                                                     FALSE /* overwrite */))) {
                }
              }
              break;
            }
            case SS: {
              int32_t sec = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &sec))) {
              } else if (0 != (ret = check_validate(&LIMITER_SECOND, sec))) {
              } else {
                ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_SEC, sec);
              }

              break;
            }
            case SSSSS: {
              int32_t sec_past_midnight = 0;
              if (0 != (ret = match_int_value(&ctx, expected_elem_len, &parsed_elem_len, &sec_past_midnight))) {
              } else if (0 != (ret = check_validate(&LIMITER_SECS_PAST_MIDNIGHT, sec_past_midnight))) {
              } else {
                int32_t secs = sec_past_midnight % (int32_t)(SECS_PER_MIN);
                int32_t mins = (sec_past_midnight / (int32_t)(SECS_PER_MIN)) % (int32_t)(MINS_PER_HOUR);
                int32_t hours = sec_past_midnight / (int32_t)(SECS_PER_MIN) / (int32_t)(MINS_PER_HOUR);
                if (0 != (ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_SEC, secs))) {
                } else if (0 != (ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_MIN, mins))) {
                } else if (0 != (ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_HOUR, hours))) {
                }
              }
              break;
            }
            case YGYYY: {
              int32_t years = 0;
              if (0 != (ret = match_int_value_with_comma(&ctx, expected_elem_len, &parsed_elem_len, &years))) {
              } else if (0 != (ret = check_validate(&LIMITER_YEAR, years))) {
              } else if (0 != (ret = set_ob_time_year_may_conflict(ob_time, &julian_year_value,
                                                            years, years, FALSE /* overwrite */))) {
              }
              break;
            }
            case SYYYY:
            case YYYY:
            case YYY:
            case YY:
            case Y: {
              int32_t years = 0;
              int32_t conflict_check_year = 0;
              int32_t sign = 1;
              if (SYYYY == elem->elem_flag_) {
                if (is_sign_char(ctx.cur_ch_[0])) {
                  sign = ('-' == ctx.cur_ch_[0] ? -1 : 1);
                  ctx.cur_ch_ += 1;
                  ctx.remain_len_ -= 1;
                } else if (part_blank1_len + part_sep_len > format_sep_len
                           && is_sign_char(ctx.cur_ch_[-1])) {
                  sign = ('-' == ctx.cur_ch_[-1] ? -1 : 1);
                }
              }
              if (!(ctx.cur_ch_ != NULL && ctx.remain_len_ > 0)) {
              } else if (0 != (ret = match_int_value_with_sign(&ctx, expected_elem_len, &parsed_elem_len, &years, sign))) {
              }
              if (0 == ret) {
                conflict_check_year = years;
                if (expected_elem_len < 4) {
                  years += (ob_time->parts_[DT_YEAR] / (int32_t)(power_of_10[parsed_elem_len]))
                      * (int32_t)(power_of_10[parsed_elem_len]);
                }
                if (0 != (ret = check_validate(&LIMITER_YEAR, years))) {
                } else if (0 != (ret = set_ob_time_year_may_conflict(ob_time, &julian_year_value,
                                                                conflict_check_year, years,
                                                                FALSE /* overwrite */))) {
                }
              }
              break;
            }
            case X: {
              if ('.' != ctx.cur_ch_[0]) {
                ignore_fs_flag = TRUE;
                ctx.cur_ch_ -= (part_blank1_len + part_sep_len + part_blank2_len);
                ctx.remain_len_ += (part_blank1_len + part_sep_len + part_blank2_len);
              } else {
                parsed_elem_len = 1;
              }
              break;
            }

            case CC:
            case SCC:
            case IW:
            case W:
            case WW:
            case YEAR:
            case SYEAR:
            case Q:
            case I:
            case IY:
            case IYY:
            case IYYY: {
              ret = 1;
              break;
            }
            default: {
              ret = 1;
              break;
            }
          } //end switch

          if (0 == ret) {
            ctx.cur_ch_ += parsed_elem_len;
            ctx.remain_len_ -= parsed_elem_len;
          }
        }//end if

        if (0 != ret) {
        } else {
        }
      } //end for

      //check if the unprocessed elems has permission to be omitted
      if (0 == ret) {
        for (; elem_idx < dfm_elems.elements; ++elem_idx) {
          struct ObDFMElem *elem = dynamic_element(&dfm_elems, elem_idx, struct ObDFMElem *);
          if (!is_element_can_omit(elem)) {
            ret = 1;
          }
        }
      }

      if (0 == ret) {
        //all elems has finished, is there anything else in str? the rest must be separators, do check.
        int64_t str_remain_sep_len = 0;
        while (str_remain_sep_len < ctx.remain_len_
               && is_split_char(ctx.cur_ch_[str_remain_sep_len])) {
          str_remain_sep_len++;
        }
        ctx.cur_ch_ += str_remain_sep_len;
        ctx.remain_len_ -= str_remain_sep_len;
        if (ctx.remain_len_ > 0) {
          ret = 1;
        }
      }

      //after noon conflict: AM PM vs HH12 HH
      if (0 == ret) {
        if (hh12_temp_value != ZERO_TIME) {
          //when  hour value, varied by meridian indicators
          //if HH12 = 12, when meridian indicator 'AM' exists, the real time is hour = 0
          //if HH12 = 12, when meridian indicator 'PM' exists, the real time is hour = 12
          if (is_after_noon) {
            ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_HOUR, hh12_temp_value % 12 + 12);
          } else {
            ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_HOUR, hh12_temp_value % 12);
          }
        }
      }

      //before christ conflict: BC AD vs YEAR   //TODO wjh: change this when ob_time support negetive years
      if (0 == ret) {
        if (is_before_christ) {
          ret = 1;
        }
      }

      //year cannot changed after this line
      //feed/validate yday + YEAR to MON and DAY
      if (0 == ret) {
        if (yday_temp_value != ZERO_DATE) {
          int32_t month = 0;
          int32_t day = 0;
          if (0 != (ret = get_day_and_month_from_year_day(yday_temp_value, ob_time->parts_[DT_YEAR], &month, &day))) {
          } else if (0 != (ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_MON, month))) {
          } else if (0 != (ret = set_ob_time_part_may_conflict(ob_time, &conflict_part_bitset, DT_MDAY, day))) {
          }
        }
      }

      //calc and validate: YDAY WDAY vs YEAR MON DAY
      if (0 == ret) {
        if (0 != (ret = validate_oracle_date(ob_time))) {
        } else {
          //ob_time_to_date func is to calc YDAY and WDAY and return DATE
          date_temp_value = ob_time_to_date(ob_time);   //TODO: shanting
          if (yday_temp_value != ZERO_DATE && ob_time->parts_[DT_YDAY] != yday_temp_value) {
            ret = 1;
          } else if (wday_temp_value != ZERO_DATE && ob_time->parts_[DT_WDAY] != wday_temp_value) {
            ret = 1;
          } else {
            ob_time->parts_[DT_DATE] = date_temp_value;
          }
        }
      }

      //for time zone info
      if (0 == ret && !ob_time->is_tz_name_valid_) {
        //B. timezone defined by time zone hour and minute
        int32_t tz_offset_value = 0;

        if ( ob_bitmap_is_set(&elem_flags, TZH)) {
          my_bool has_tzh_value = (temp_tzh_value >= 0);
          my_bool has_tzm_value = (temp_tzm_value >= 0);
          if (!has_tzh_value && has_tzm_value) {
            ret = 1;
          } else if (!has_tzh_value && !has_tzm_value) {
            //do nothing
          } else if (has_tzh_value && !has_tzm_value) {
            tz_hour = temp_tz_factor * temp_tzh_value;
            tz_min = temp_tz_factor * abs(tz_min);
          } else {
            tz_hour = temp_tz_factor * temp_tzh_value;
            tz_min = temp_tz_factor * temp_tzm_value;
          }
        } else if ( ob_bitmap_is_set(&elem_flags, TZR)) {
          //do nothing
        } else {
          //no time zone info in elem_flags
        }

        //calc offset
        if (0 == ret) {
          if (tz_hour * tz_min < 0) {
            ret = 1;
          } else {
            tz_offset_value = (int32_t)(tz_hour * MINS_PER_HOUR + tz_min);
          }
        }

        //final validate
        if (0 == ret) {
          if (!(MIN_OFFSET_MINUTES <= tz_offset_value && tz_offset_value <= MAX_OFFSET_MINUTES)) {
            ret = 1;
          } else {
            ob_time->parts_[DT_OFFSET_MIN] = tz_offset_value;
          }
        }
      }

      if (0 != ret) {
      } else {
      }
    }//end if
    ob_bitmap_free(&elem_flags);
    ma_delete_dynamic(&dfm_elems);
  }

  return ret;
}
