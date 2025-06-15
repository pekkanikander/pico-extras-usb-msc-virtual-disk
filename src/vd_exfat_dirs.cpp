
#include "vd_exfat.h"
#include "vd_exfat_params.h"

/// Create an exFAT 32-bit timestamp (Table 29 §7.4.8)
/// year: full year (>=1980), month: 1-12, day:1-31,
/// hour:0-23, minute:0-59, second:0-59 (rounded down to even).
static constexpr exfat_timestamp_t make_exfat_timestamp(
    unsigned year,
    unsigned month,
    unsigned day,
    unsigned hour,
    unsigned minute,
    unsigned second) {
    return ((year - 1980)   & 0x7F) << 25
         | (month           & 0x0F) << 21
         | (day             & 0x1F) << 16
         | (hour            & 0x1F) << 11
         | (minute          & 0x3F) <<  5
         | ((second / 2)    & 0x1F) <<  0;
}
