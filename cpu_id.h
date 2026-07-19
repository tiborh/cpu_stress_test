#ifndef CPU_ID_H
#define CPU_ID_H

#include <stddef.h>

/**
 * Retrieves a sanitized, filename-safe CPU identifier slug.
 * Example output: "intel_n150" or "amd_ryzen_7_5800x".
 *
 * Parameters:
 *   - buffer: Pointer to the destination character array.
 *   - max_len: Maximum size of the destination buffer.
 */
void get_cpu_identifier(char *buffer, size_t max_len);

#endif /* CPU_ID_H */
