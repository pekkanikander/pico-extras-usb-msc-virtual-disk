
#include "vd_exfat.h"
#include <time.h>

#ifndef __INTELLISENSE__
  // in real builds, expand to the C11 keyword
  #define STATIC_ASSERT_PACKED(cond, msg) _Static_assert(cond, msg)
#else
  // under IntelliSense, drop all these checks
  #define STATIC_ASSERT_PACKED(cond, msg)
#endif

// exFAT directory-entry type codes (entry_type field)
typedef enum {
    exfat_entry_type_end_of_directory  = 0x00, ///< End-of-directory marker
    exfat_entry_type_unused            = 0x01, ///< Unused entry
    exfat_entry_type_allocation_bitmap = 0x81, ///< Allocation Bitmap
    exfat_entry_type_upcase_table      = 0x82, ///< Up-case Table
    exfat_entry_type_volume_label      = 0x83, ///< Volume Label
    exfat_entry_type_file_directory    = 0x85, ///< File Directory Entry
    exfat_entry_type_volume_guid       = 0xA0, ///< Volume GUID entry
    exfat_entry_type_stream_extension  = 0xC0, ///< Stream Extension Entry
    exfat_entry_type_file_name         = 0xC1, ///< File Name Entry
} exfat_entry_type_t;

// exFAT Generic Directory Entry (32 bytes)
/// See Microsoft spec § 6.2 “Generic DirectoryEntry template” (Table 14)
typedef struct __packed {
    exfat_entry_type_t entry_type;        ///< Directory entry type code
    uint8_t            entry_specific[19];///< Reserved or structure-specific data
    uint32_t           first_cluster;     ///< FirstCluster: starting cluster of this entry’s data (if any)
    uint64_t           data_length;       ///< DataLength: size in bytes of this entry’s data (if any)
} exfat_generic_dir_entry_t;
STATIC_ASSERT_PACKED(sizeof(exfat_generic_dir_entry_t) == 32,
   "Generic exFAT directory entry must be 32 bytes");

/// exFAT Allocation Bitmap Directory Entry (entry_type = 0x81)
/// See Microsoft spec §7.1 “Allocation Bitmap Directory Entry” (Table 20)
typedef struct __packed {
    exfat_entry_type_t entry_type;   ///< Entry type (0x81)
    uint8_t            bitmap_flags; ///< BitmapFlags (bitmask of valid clusters)
    uint8_t            reserved1[18]; ///< Reserved; must be zero
    uint32_t           first_cluster;///< Cluster number of the allocation bitmap file
    uint64_t           data_length;  ///< Size in bytes of the allocation bitmap
} exfat_allocation_bitmap_dir_entry_t;
STATIC_ASSERT_PACKED(sizeof(exfat_allocation_bitmap_dir_entry_t) == 32,
    "Allocation Bitmap exFAT directory entry must be 32 bytes");

/// exFAT Up-case Table Directory Entry (entry_type = 0x82)
/// See Microsoft spec §7.2 “Up-case Table Directory Entry” (Table 23)
typedef struct __packed {
    exfat_entry_type_t entry_type;    ///< Entry type (0x82)
    uint8_t            reserved1[3];  ///< Reserved; must be zero
    uint32_t           table_checksum;///< Checksum of the up-case table data
    uint8_t            reserved2[12]; ///< Reserved; must be zero
    uint32_t           first_cluster; ///< Cluster number of the up-case table file
    uint64_t           data_length;   ///< Size in bytes of the up-case table
} exfat_upcase_table_dir_entry_t;
STATIC_ASSERT_PACKED(sizeof(exfat_upcase_table_dir_entry_t) == 32,
    "Up-case Table exFAT directory entry must be 32 bytes");

/// exFAT Volume Label Directory Entry (entry_type = 0x83)
/// See Microsoft spec §7.3 “Volume Label Directory Entry” (Table 26)
typedef struct __packed {
    exfat_entry_type_t entry_type;    ///< Entry type (0x83)
    uint8_t            char_count;    ///< Character count of the volume label (0–11)
    char16_t           volume_label[11]; ///< Volume label in UTF-16 (padded with 0)
    uint8_t            reserved[8];   ///< Reserved; must be zero
} exfat_volume_label_dir_entry_t;
STATIC_ASSERT_PACKED(sizeof(exfat_volume_label_dir_entry_t) == 32,
    "Volume Label exFAT directory entry must be 32 bytes");

/// exFAT Volume GUID Directory Entry (entry_type = 0xA0)
/// See Microsoft spec §7.5 “Volume GUID Directory Entry” (Table 32)
typedef struct __packed {
    exfat_entry_type_t entry_type;           ///< Entry type (0xA0)
    uint8_t            secondary_count;      ///< SecondaryCount; must be 0
    uint16_t           set_checksum;         ///< SetChecksum
    uint16_t           general_primary_flags;///< GeneralPrimaryFlags; must be 0
    uint8_t            volume_guid[16];      ///< Volume GUID (all values valid except null GUID)
    uint8_t            reserved[10];         ///< Reserved; must be zero
} exfat_volume_guid_dir_entry_t;
STATIC_ASSERT_PACKED(sizeof(exfat_volume_guid_dir_entry_t) == 32,
    "Volume GUID exFAT directory entry must be 32 bytes");

/// exFAT first root directory entries
typedef struct __packed exfat_root_dir_entries_first {
    exfat_volume_label_dir_entry_t      volume_label;
    exfat_allocation_bitmap_dir_entry_t allocation_bitmap;
    exfat_upcase_table_dir_entry_t      upcase_table;
} exfat_root_dir_entries_first_t;

STATIC_ASSERT_PACKED(sizeof(exfat_root_dir_entries_first_t) == 3 * 32, "must be 3 * 32 bytes");

/// exFAT timestamp field (32 bits; see Table 29 §7.4.8)
typedef uint32_t exfat_timestamp_t;

/// §7.4.9 “10msIncrement Fields"
/// These fields are not used in this read-only volume (always zero).

/// §7.4.10 “UtcOffset Fields" (signed 15-minute increments, Table 31)
/// We only support UTC (offset = 0 minutes).
typedef enum {
    exfat_utc_offset_UTC = 0x80  ///< OffsetValid | Coordinated Universal Time
} exfat_utc_offset_t;

/// exFAT File Directory Entry (entry_type = 0x85)
/// See Microsoft spec §7.4 “File Directory Entry” (Table 27)
typedef struct __packed {
    exfat_entry_type_t   entry_type;        ///< Entry type (0x85)
    uint8_t              secondary_count;   ///< Number of secondary entries
    uint16_t             set_checksum;      ///< Checksum of the set of entries
    uint16_t             file_attributes;   ///< File attributes
    uint8_t              reserved1[2];      ///< Reserved; must be zero
    exfat_timestamp_t    creat_time;        ///< Creation time
    exfat_timestamp_t    last_mod_time;     ///< Last modification time
    exfat_timestamp_t    last_acc_time;     ///< Last access time
    uint8_t              creat_time_ms;     ///< Creation time 10ms increments
    uint8_t              last_mod_time_ms;  ///< Last modification time 10ms increments
    exfat_utc_offset_t   creat_time_off;    ///< Creation time UTC offset
    exfat_utc_offset_t   last_mod_time_off; ///< Last modification time UTC offset
    exfat_utc_offset_t   last_acc_time_off; ///< Last access time UTC offset
    uint8_t              reserved2[7];      ///< Reserved; must be zero
} exfat_file_directory_dir_entry_t;
STATIC_ASSERT_PACKED(sizeof(exfat_file_directory_dir_entry_t) == 32,
    "File Directory exFAT directory entry must be 32 bytes");


/// exFAT Stream Extension Directory Entry (entry_type = 0xC0)
/// See Microsoft spec §7.6 “Stream Extension Directory Entry” (Table 33)
typedef struct __packed {
    exfat_entry_type_t entry_type;             ///< Entry type (0xC0)
    uint8_t            secondary_flags;        ///< GeneralSecondaryFlags; AllocationPossible must be 1
    uint8_t            reserved1;              ///< Reserved; must be zero
    uint8_t            name_length;            ///< NameLength: length of file name in bytes (1–255)
    uint16_t           name_hash;              ///< NameHash: hash of the up-cased file name
    uint16_t           reserved2;              ///< Reserved; must be zero
    uint64_t           valid_data_length;      ///< ValidDataLength: count of valid bytes written
    uint32_t           reserved3;              ///< Reserved; must be zero
    uint32_t           first_cluster;          ///< FirstCluster: starting cluster of the data stream
    uint64_t           data_length;            ///< DataLength: total size in bytes of the data stream
} exfat_stream_extension_dir_entry_t;
STATIC_ASSERT_PACKED(sizeof(exfat_stream_extension_dir_entry_t) == 32,
    "Stream Extension exFAT directory entry must be 32 bytes");

/// exFAT File Name Directory Entry (entry_type = 0xC1)
/// See Microsoft spec §7.7 “File Name Directory Entry” (Table 34)
typedef struct __packed {
    exfat_entry_type_t entry_type;            ///< Entry type (0xC1)
    uint8_t            general_secondary_flags;///< GeneralSecondaryFlags; AllocationPossible must be 0
    char16_t           file_name[15];         ///< FileName: array of 15 UTF-16 characters
} exfat_file_name_dir_entry_t;
STATIC_ASSERT_PACKED(sizeof(exfat_file_name_dir_entry_t) == 32,
    "File Name exFAT directory entry must be 32 bytes");

/// Compile time generated exFAT root directory entry sets
typedef struct __packed vd_static_file_s {
    exfat_file_directory_dir_entry_t      file_dir_entry;
    exfat_stream_extension_dir_entry_t    stream_extension_entry;
    exfat_file_name_dir_entry_t           file_name_entry;
} vd_static_file_t;
STATIC_ASSERT_PACKED(sizeof(vd_static_file_t) == 3 * 32,
    "Fixed exFAT file/directory entry set length must be == 3 * 32 bytes");

/// Dynamically generated exFAT root directory entry sets
typedef struct __packed exfat_root_dir_entries_dynamic_file {
    exfat_file_directory_dir_entry_t      file_directory;    // 32 bytes
    exfat_stream_extension_dir_entry_t    stream_extension;  // 32 bytes
    exfat_file_name_dir_entry_t           file_name[10];      // 10 * 32 bytes, 9 * 15 chars > 127 chars
} exfat_root_dir_entries_dynamic_file_t;
STATIC_ASSERT_PACKED(sizeof(exfat_root_dir_entries_dynamic_file_t) == 12 * 32,
    "Dynamic exFAT file/directory entry set length must be == 12 * 32 bytes");
STATIC_ASSERT_PACKED(sizeof(exfat_root_dir_entries_dynamic_file_t) % CFG_TUD_MSC_EP_BUFSIZE == 0,
    "Dynamic exFAT file/directory entry set length must be a multiple of MSC EP buffer size");

#ifdef __cplusplus
#define static_cast(type) static_cast<type>
#else
#define constexpr
#define static_cast(type) (type)
#endif


/// Compute the name hash for an exFAT file name.
static constexpr inline uint16_t vd_exfat_dirs_compute_name_hash(const char16_t *name, size_t len) {
    uint16_t hash = 0;
    for (size_t i = 0; i < len; ++i) {
        char16_t wc = name[i];
        uint8_t lo = static_cast(uint8_t)(wc & 0xFF);
        uint8_t hi = static_cast(uint8_t)((wc >> 8) & 0xFF);
        hash = static_cast(uint16_t)(((hash & 1) ? 0x8000 : 0) + (hash >> 1) + lo);
        hash = static_cast(uint16_t)(((hash & 1) ? 0x8000 : 0) + (hash >> 1) + hi);
    }
    return hash;
}

// Helper functions for exFAT timestamp computation
static constexpr inline bool vd_exfat_dirs_is_leap_year(int year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static constexpr inline int vd_exfat_dirs_days_in_year(int year) {
    return 365 + vd_exfat_dirs_is_leap_year(year);
}

static constexpr inline int vd_exfat_dirs_days_in_month(int year, int month) {
    // month: 0=Jan, ..., 11=Dec
    if (month == 1) { // February
        return vd_exfat_dirs_is_leap_year(year) ? 29 : 28;
    }
    // April, June, September, November have 30 days
    if (month == 3 || month == 5 || month == 8 || month == 10) {
        return 30;
    }
    // All others have 31 days
    return 31;
}

static constexpr inline struct tm vd_exfat_dirs_make_tm(time_t epoch_seconds) {
    struct tm tm = {0};
    int days = epoch_seconds / 86400;
    int rem  = epoch_seconds % 86400;

    tm.tm_hour = rem / 3600;
    rem = rem % 3600;
    tm.tm_min = rem / 60;
    tm.tm_sec = rem % 60;

    // Start from 1970
    int year = 1970;
    for (int ydays = vd_exfat_dirs_days_in_year(year); days >= ydays; ydays = vd_exfat_dirs_days_in_year(year)) {
        days -= ydays;
        year++;
    }
    tm.tm_year = year - 1900;

    // Now find the month
    int month = 0;
    for (int mdays = vd_exfat_dirs_days_in_month(year, month); days >= mdays; mdays = vd_exfat_dirs_days_in_month(year, month)) {
        days -= mdays;
        month++;
    }
    tm.tm_mon = month;
    tm.tm_mday = days + 1;

    return tm;
}

/// Create an exFAT 32-bit timestamp from Unix epoch seconds (Table 29 §7.4.8)
static constexpr inline exfat_timestamp_t vd_exfat_dirs_make_timestamp(time_t epoch_seconds) {
    // exFAT timestamp fields: year (>=1980), month: 1-12, day:1-31, hour:0-23, minute:0-59, second:0-59 (rounded down to even)
    const struct tm tm = vd_exfat_dirs_make_tm(epoch_seconds);

    const unsigned year   = (tm.tm_year + 1900) < 1980 ? 1980 : (tm.tm_year + 1900);
    const unsigned month  = tm.tm_mon + 1;
    const unsigned day    = tm.tm_mday;
    const unsigned hour   = tm.tm_hour;
    const unsigned minute = tm.tm_min;
    const unsigned second = tm.tm_sec;
    return ((year - 1980)   & 0x7F) << 25
         | (month           & 0x0F) << 21
         | (day             & 0x1F) << 16
         | (hour            & 0x1F) << 11
         | (minute          & 0x3F) <<  5
         | ((second / 2)    & 0x1F) <<  0;
}



#ifdef __cplusplus
extern "C" {
#endif

// Internal API for dynamic file management (not for external use)

int vd_exfat_dir_add_file(vd_dynamic_file_t* file); // >= 0 if success, -1 if error
int vd_exfat_dir_update_file(vd_dynamic_file_t* file);    // >= 0 if success, -1 if error

#ifdef __cplusplus
}
#endif
