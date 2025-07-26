
#ifndef _PICO_STDIO_RING_BUFFER_H_
#define _PICO_STDIO_RING_BUFFER_H_

#include <stdint.h>
#include <stddef.h>

#include <pico/stdio.h>

typedef struct ring_buffer_s ring_buffer_t;

typedef void (*ring_buffer_notify_write_cb_t)(struct ring_buffer_s *const rb, size_t bytes_written, size_t total_bytes_written);
struct ring_buffer_s {
    uint8_t *const beg; ///< Beginning of the ring buffer
    uint8_t *const end; ///< End of the ring buffer
    uint8_t *      ptr; ///< Current writing position
    size_t         tot; ///< Total bytes written
    ring_buffer_notify_write_cb_t notify_write_cb; ///< Callback on writes
};

static inline size_t ring_buffer_capacity(const ring_buffer_t *const rb) {
    return rb->end - rb->beg;
}

static inline size_t ring_buffer_total_written(const ring_buffer_t *const rb) {
    return rb->tot;
}


/** \brief Support for stdout to a SRAM ring buffer
 *  \defgroup pico_stdio_ring_buffer pico_stdio_ring_buffer
 *  \ingroup pico_stdio
 *
 *  Linking this library or calling `pico_enable_stdio_ring_buffer(TARGET ENABLED)`
 *  in the CMake (which achieves the same thing) will add SRAM ring buffer
 *  to the drivers used for standard .output
 *
 *  Note this library is a developer convenience.  It is not applicable in all cases.
 */

// PICO_CONFIG: PICO_STDIO_RING_BUFFER_LEN, Set ring buffer length, default=4k, group=pico_stdio_ring_buffer
#ifndef PICO_STDIO_RING_BUFFER_LEN
#define PICO_STDIO_RING_BUFFER_LEN (4 * 1024)
#endif

// PICO_CONFIG: PICO_STDIO_RING_BUFFER_WRITE_SHORT_LEN, Set ring buffer short write length, default=8, group=pico_stdio_ring_buffer
#ifndef PICO_STDIO_RING_BUFFER_WRITE_SHORT_LEN
#define PICO_STDIO_RING_BUFFER_WRITE_SHORT_LEN 8  // Heuristics, should be measured
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern ring_buffer_t stdio_ring_buffer_rb;

extern stdio_driver_t stdio_ring_buffer;

/*! \brief Explicitly initialize Ring Buffer stdio and
           add it to the current set of stdin drivers
 *  \ingroup pico_stdio_ring_buffer
 *
 *  \return true always
 */
bool stdio_ring_buffer_init(ring_buffer_notify_write_cb_t notify_write_cb);

/*! \brief Explicitly deinitialize Ring Buffer stdio and
           remove it from the current set of stdin drivers
 *  \ingroup pico_stdio_ring_buffer
 *
 *  \return true if all dependent features were removed, false if an error occurred
 */
bool stdio_ring_buffer_deinit(void);

/**
 * \brief Read data from the ring buffer's virtual stream into a caller buffer.
 *
 * This function treats the buffer as a virtual, append-only byte stream,
 * where older bytes beyond \p capacity are discarded.
 * It copies up to \p len bytes from the virtual stream,
 * starting at \p offset into \p buf, handling wrap-around transparently.
 * Any parts of the requested range that lie before the start of
 * stored data or beyond the end of the stream are not copied, and those
 * positions in \p buf remain unchanged.
 *
 * \param offset  Byte offset within the virtual stream to start reading.
 * \param buf     Destination buffer where read bytes will be stored.
 * \param len     Maximum number of bytes to read.
 * \return        Actual number of bytes copied into \p buf.
 */
size_t stdio_ring_buffer_get_data(size_t offset, uint8_t *const buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif  // _PICO_STDIO_RING_BUFFER_H_
