#include <string.h>
#include <time.h>
#include <pico/time.h>

#include "picovd_config.h"
#include "vd_virtual_disk.h"
#include "stdio_ring_buffer.h"
#include "tusb_config.h"

#ifndef PICOVD_STDOUT_FILE_NAME
#define PICOVD_STDOUT_FILE_NAME "STDOUT.TXT"
#endif
#ifndef PICOVD_STDOUT_TAIL_FILE_NAME
#define PICOVD_STDOUT_TAIL_FILE_NAME "STDOUT-TAIL.TXT"
#endif
#ifndef PICOVD_STDOUT_TAIL_UA_MINIMUM_AMOUNT
#define PICOVD_STDOUT_TAIL_UA_MINIMUM_AMOUNT 128
#endif
#ifndef PICOVD_STDOUT_TAIL_UA_DELAY_SEC
#define PICOVD_STDOUT_TAIL_UA_DELAY_SEC 10
#endif
#ifndef PICOVD_STDOUT_TAIL_UA_TIMEOUT_SEC
#define PICOVD_STDOUT_TAIL_UA_TIMEOUT_SEC 30
#endif

#define STDOUT_NOTIFY_THRESHOLD 512

// Timer to notify the host every UA_TIMEOUT_SEC seconds if no data has been written and not read
static alarm_id_t tail_timeout_alarm = 0;

/// -- STDOUT.TXT (classic growing log file semantics) --
static int32_t stdout_file_content_cb(uint32_t offset, void* buf, uint32_t bufsize) {
    memset(buf, 0, bufsize); // XXX TODO: is this needed?
    return stdio_ring_buffer_get_data(offset, buf, bufsize);
}

// --- STDOUT-TAIL.TXT (tail -F semantics) ---
static size_t stdout_tail_total_read = 0;
static time_t stdout_tail_last_read_time = 0;
static volatile int stdout_tail_ua_pending = 0;

static size_t tail_file_window_start = 0; // Absolute offset in the stream
static size_t tail_file_window_size = 0;  // Always a multiple of CFG_TUD_MSC_EP_BUFSIZE

static int32_t stdout_tail_file_content_cb(uint32_t offset, void* buf, uint32_t bufsize) {
    // Only allow reads within the current window
    if (offset >= tail_file_window_size) {
        return 0;
    }
    // Clamp to available data in the window
    size_t to_copy = bufsize;
    if (offset + bufsize > tail_file_window_size) {
        to_copy = tail_file_window_size - offset;
    }
    if (tail_file_window_start + offset + to_copy > stdout_tail_total_read) {
        stdout_tail_total_read = tail_file_window_start + offset + to_copy;
    }
    int rc = stdio_ring_buffer_get_data(tail_file_window_start + offset, buf, to_copy);
    return rc;
}

PICOVD_DEFINE_FILE_RUNTIME(
    stdout_dynamic_file,
    PICOVD_STDOUT_FILE_NAME,
    0, // initial size, will be updated
    stdout_file_content_cb
);

PICOVD_DEFINE_FILE_RUNTIME(
    stdout_dynamic_tail_file,
    PICOVD_STDOUT_TAIL_FILE_NAME,
    0, // initial size, will be updated
    stdout_tail_file_content_cb
);

// Update file sizes and trigger SCSI UA 0x28 (media change)
static void notify_files_changed(size_t total_bytes_written) {
    size_t unread = total_bytes_written - stdout_tail_total_read;
    // Truncate to previous multiple of CFG_TUD_MSC_EP_BUFSIZE
    size_t rounded_unread = (unread / CFG_TUD_MSC_EP_BUFSIZE) * CFG_TUD_MSC_EP_BUFSIZE;
    // The window starts at the oldest unread byte that fits in the rounded size
    tail_file_window_start = stdout_tail_total_read;
    tail_file_window_size = rounded_unread;
    vd_update_file(&stdout_dynamic_tail_file, rounded_unread);
    vd_update_file(&stdout_dynamic_file, total_bytes_written);
    stdout_tail_ua_pending++;
}

static int64_t ua_timeout_cb(alarm_id_t id, void* user_data) {
    notify_files_changed(ring_buffer_total_written(&stdio_ring_buffer_rb));
    tail_timeout_alarm = 0;
    return 0;
}

// --- Notification and UA logic ---
static void stdout_notify_write_cb(ring_buffer_t *const rb, size_t bytes_written, size_t total_bytes_written) {
    size_t unread = total_bytes_written - stdout_tail_total_read;
    // If host hasn't read new data for UA delay, schedule UA
    time_t now = to_ms_since_boot(get_absolute_time())/1000;;
    if (unread > PICOVD_STDOUT_TAIL_UA_MINIMUM_AMOUNT) {
      if (stdout_tail_ua_pending == 0 && (now - stdout_tail_last_read_time) >= PICOVD_STDOUT_TAIL_UA_DELAY_SEC) {
        notify_files_changed(total_bytes_written);
      } else {
        // Schedule UA if not already scheduled
        if (tail_timeout_alarm == 0) {
            tail_timeout_alarm = add_alarm_in_ms(PICOVD_STDOUT_TAIL_UA_TIMEOUT_SEC, ua_timeout_cb, NULL, true);
        }
      }
    }
}

void vd_files_stdout_init(void) {
    stdio_ring_buffer_init(stdout_notify_write_cb);
    vd_add_file(&stdout_dynamic_file, 10 * 1024 * 1024);
    vd_add_file(&stdout_dynamic_tail_file, 10 * 1024 * 1024);
    // Initialize file sizes
    stdout_notify_write_cb(&stdio_ring_buffer_rb, 0, ring_buffer_total_written(&stdio_ring_buffer_rb));
}
