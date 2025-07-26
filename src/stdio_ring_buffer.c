#include <assert.h>
#include <string.h>

#include <pico/mutex.h>
#include <pico/stdio/driver.h>

#include "stdio_ring_buffer.h"

static uint8_t stdio_ring_buffer_data[PICO_STDIO_RING_BUFFER_LEN];

ring_buffer_t stdio_ring_buffer_rb = {
    .beg = stdio_ring_buffer_data,
    .end = stdio_ring_buffer_data + PICO_STDIO_RING_BUFFER_LEN,
    .ptr = stdio_ring_buffer_data,
    .tot = 0,
};
static mutex_t stdio_ring_buffer_mutex;

// Check ring buffer consistency
static inline void ring_buffer_assert(const ring_buffer_t *const rb) {
    assert(rb->beg <= rb->ptr);
    assert(rb->ptr < rb->end);
    assert(rb->tot % ring_buffer_capacity(rb) == rb->ptr - rb->beg);
}

// Returns # actually stored, if # < len, then some were discarded
static size_t ring_buffer_write(ring_buffer_t *const rb, const uint8_t *buf, size_t len) {
    ring_buffer_assert(rb);

    // Update the total-bytes counter
    rb->tot += len;

    // capacity of the buffer
    const size_t capacity = ring_buffer_capacity(rb);

    // If the write is larger than the buffer, only keep the *last* 'capacity' bytes:
    if (len > capacity) {
        buf  += len - capacity;
        len   = capacity;
    }
    // Now 0 < len <= capacity

    if (len < PICO_STDIO_RING_BUFFER_WRITE_SHORT_LEN) {
        while (len--) {
            *(rb->ptr++) = *buf++;
            if (rb->ptr == rb->end) { // Should never be larger
                rb->ptr = rb->beg;
            }
        }
    } else {
        // How many bytes from ptr to the end of the array?
        size_t to_end = rb->end - rb->ptr;

        if (len < to_end) {
            // Single contiguous write
            memcpy(rb->ptr, buf, len);
            rb->ptr += len;
        } else {
            // Split into two writes
            memcpy(rb->ptr, buf,  to_end);
            // If len == to_end, the following memcpy is a NOP
            // but allows us to drop an if from above
            memcpy(rb->beg, buf + to_end, len - to_end);
            rb->ptr = rb->beg + (len - to_end);
        }
    }

    ring_buffer_assert(rb);
    if (rb->notify_write_cb) {
        rb->notify_write_cb(rb, len, rb->tot);
    }
    return len;
}

static size_t ring_buffer_get(const ring_buffer_t *const rb, size_t offset, uint8_t *buf, size_t len) {
    ring_buffer_assert(rb);

    const size_t capacity = ring_buffer_capacity(rb);
    const size_t start_offset = (rb->tot > capacity) ? (rb->tot - capacity) : 0;
    const size_t end_offset = rb->tot;

    // At any point of time, the ring buffer contains bytes
    // from rb->tot - capacity to rb->tot - 1 (starting index at zero)

    // No overlap: requested range is entirely before start or after end
    if (offset >= end_offset || offset + len <= start_offset) {
        return 0;
    }

    // Clamp to the available range
    const size_t actual_start = offset < start_offset ? start_offset : offset;
    const size_t actual_end = (offset + len > end_offset) ? end_offset : (offset + len);
    const size_t actual_len = actual_end - actual_start;

    // Compute offset into caller's buf where copy should begin
    const size_t buf_offset = actual_start > offset ? (actual_start - offset) : 0;

    // Copy from the circular buffer using modulo-based indexing
    const size_t start_idx = actual_start % capacity;
    const size_t first_chunk_len = capacity - start_idx;
    if (actual_len <= first_chunk_len) {
        memcpy(buf + buf_offset, rb->beg + start_idx, actual_len);
    } else {
        memcpy(buf + buf_offset, rb->beg + start_idx, first_chunk_len);
        memcpy(buf + buf_offset + first_chunk_len, rb->beg, actual_len - first_chunk_len);
    }

    return actual_len;
}

static void stdio_ring_buffer_out_chars(const char *buf, int len) {
    mutex_enter_blocking(&stdio_ring_buffer_mutex);
    ring_buffer_write(&stdio_ring_buffer_rb, buf, len);
    mutex_exit(&stdio_ring_buffer_mutex);
}

static void stdio_ring_buffer_out_flush(void) {
    // Currently no-op
}

static int stdio_ring_buffer_in_chars(char *buf, int length) {
    int rc = PICO_ERROR_NO_DATA;
    return rc;
}

stdio_driver_t stdio_ring_buffer = {
    .out_chars = stdio_ring_buffer_out_chars,
    .out_flush = stdio_ring_buffer_out_flush,
    .in_chars  = stdio_ring_buffer_in_chars,
};

bool stdio_ring_buffer_init(ring_buffer_notify_write_cb_t notify_write_cb) {
    stdio_ring_buffer_rb.notify_write_cb = notify_write_cb;
    if (!mutex_is_initialized(&stdio_ring_buffer_mutex)) {
        mutex_init(&stdio_ring_buffer_mutex);
    }

    stdio_set_driver_enabled(&stdio_ring_buffer, true);

    return true;
}

bool stdio_ring_buffer_deinit(void) {
    // XXX TODO: Add calling back to dependent drivers
    stdio_set_driver_enabled(&stdio_ring_buffer, false);
    return true;
}

size_t stdio_ring_buffer_get_data(size_t offset, uint8_t *const buf, size_t len) {
    mutex_enter_blocking(&stdio_ring_buffer_mutex);
    const size_t rc = ring_buffer_get(&stdio_ring_buffer_rb, offset, buf, len);
    mutex_exit(&stdio_ring_buffer_mutex);
    return rc;
}
