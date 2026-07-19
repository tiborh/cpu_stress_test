#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <stddef.h>

/**
 * Retrieves the current system timestamp in "yyyymmdd_hhmmss" 24-hour format.
 * Example: "20260606_182916"
 * 
 * Parameters:
 *   - buffer: Pointer to the destination character array.
 *   - max_len: Maximum size of the destination buffer (needs at least 16 bytes).
 */
void get_current_timestamp(char *buffer, size_t max_len);

#endif /* TIMESTAMP_H */
