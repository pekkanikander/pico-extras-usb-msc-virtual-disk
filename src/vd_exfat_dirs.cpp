
#include <pico/bootrom.h>

#include "picovd_config.h"

#include "vd_exfat_params.h"
#include "vd_exfat.h"
#include "vd_exfat_dirs.h"

// Primary File Directory entry: one stream extension + one filename entry
static constexpr exfat_file_directory_dir_entry_t file_dir_entry = {
    .entry_type      = exfat_entry_type_file_directory,
    .secondary_count = 2u,
    .set_checksum    = 0u,  // Computed lazily at runtime
    .file_attributes = EXFAT_FILE_ATTR_READ_ONLY,
};

#if PICOVD_SRAM_ENABLED

// ---------------------------------------------------------------------------
// Compile-time directory entries for SRAM.BIN
// ---------------------------------------------------------------------------

// Stream Extension entry: holds size, start cluster, name length & hash
static constexpr exfat_stream_extension_dir_entry_t sram_stream_entry = {
    .entry_type        = exfat_entry_type_stream_extension,
    .secondary_flags   = 0x03, // XXX
    .name_length       = PICOVD_SRAM_FILE_NAME_LEN,
    .name_hash         = exfat_dirs_compute_name_hash(PICOVD_SRAM_FILE_NAME, PICOVD_SRAM_FILE_NAME_LEN),
    .valid_data_length = static_cast<uint64_t>(PICOVD_SRAM_SIZE_BYTES),
    .first_cluster     = PICOVD_SRAM_START_CLUSTER,
    .data_length       = static_cast<uint64_t>(PICOVD_SRAM_SIZE_BYTES),
};

static constexpr exfat_file_name_dir_entry_t sram_file_name_entry = {
    .entry_type = exfat_entry_type_file_name,
    .file_name  = PICOVD_SRAM_FILE_NAME,
};

extern "C" constexpr exfat_root_dir_entries_fixed_file_t exfat_root_dir_sram_file_data = {
    file_dir_entry,        // Primary File Directory entry
    sram_stream_entry,          // Stream Extension entry
  { sram_file_name_entry, },   // File Name entry
};

#endif // PICOVD_SRAM_ENABLED

#if PICOVD_BOOTROM_ENABLED

// ---------------------------------------------------------------------------
// Compile-time directory entries for BOOTROM.BIN
// ---------------------------------------------------------------------------

// Stream Extension entry: holds size, start cluster, name length & hash
static constexpr exfat_stream_extension_dir_entry_t bootrom_stream_entry = {
    .entry_type        = exfat_entry_type_stream_extension,
    .secondary_flags   = 0x03, // XXX
    .name_length       = PICOVD_BOOTROM_FILE_NAME_LEN,
    .name_hash         = exfat_dirs_compute_name_hash(PICOVD_BOOTROM_FILE_NAME, PICOVD_BOOTROM_FILE_NAME_LEN),
    .valid_data_length = static_cast<uint64_t>(PICOVD_BOOTROM_SIZE_BYTES),
    .first_cluster     = PICOVD_BOOTROM_START_CLUSTER,
    .data_length       = static_cast<uint64_t>(PICOVD_BOOTROM_SIZE_BYTES),
};

static constexpr exfat_file_name_dir_entry_t bootrom_file_name_entry = {
    .entry_type = exfat_entry_type_file_name,
    .file_name  = PICOVD_BOOTROM_FILE_NAME,
};

extern "C" constexpr exfat_root_dir_entries_fixed_file_t exfat_root_dir_bootrom_file_data = {
    file_dir_entry,
    bootrom_stream_entry,
  { bootrom_file_name_entry, },
};

#endif // PICOVD_BOOTROM_ENABLED

#if PICOVD_FLASH_ENABLED

// ---------------------------------------------------------------------------
// Compile-time directory entries for FLASH.BIN
// ---------------------------------------------------------------------------

// Stream Extension entry: holds size, start cluster, name length & hash
static constexpr exfat_stream_extension_dir_entry_t flash_stream_entry = {
    .entry_type        = exfat_entry_type_stream_extension,
    .secondary_flags   = 0x03, // XXX
    .name_length       = PICOVD_FLASH_FILE_NAME_LEN,
    .name_hash         = exfat_dirs_compute_name_hash(PICOVD_FLASH_FILE_NAME, PICOVD_FLASH_FILE_NAME_LEN),
    .valid_data_length = static_cast<uint64_t>(PICOVD_FLASH_SIZE_BYTES),
    .first_cluster     = PICOVD_FLASH_START_CLUSTER,
    .data_length       = static_cast<uint64_t>(PICOVD_FLASH_SIZE_BYTES),
};

static constexpr exfat_file_name_dir_entry_t flash_file_name_entry = {
    .entry_type = exfat_entry_type_file_name,
    .file_name  = PICOVD_FLASH_FILE_NAME,
};

extern "C" constexpr exfat_root_dir_entries_fixed_file_t exfat_root_dir_flash_file_data = {
    file_dir_entry,
    flash_stream_entry,
  { flash_file_name_entry, },
};

#endif // PICOVD_FLASH_ENABLED
