#ifndef VD_FILES_STDOUT_H
#define VD_FILES_STDOUT_H

#include <stddef.h>
#include <stdint.h>

// Configurable parameters (move from .c if needed by other modules)
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

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the stdout file/ring buffer system
void vd_files_stdout_init(void);

#ifdef __cplusplus
}
#endif

#endif // VD_FILES_STDOUT_H
