#ifndef CPU_TEMP_H
#define CPU_TEMP_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Retrieves the CPU temperature in degrees Celsius.
 *
 * config_path     - Path to cpu_temp.conf, or NULL to skip config.
 * config_primary  - If true, consult config before auto-detection;
 *                   if false (normal), use config only as fallback when
 *                   auto-detection finds nothing.
 * verbose         - If true, narrate detection steps to stdout.
 *
 * Returns the temperature in Celsius, or -1.0 on failure.
 */
double get_cpu_temperature(const char *config_path, bool config_primary, bool verbose);

/**
 * Resolves and retrieves the file path where CPU temperature is read from.
 *
 * config_path     - Path to cpu_temp.conf, or NULL to skip config.
 * config_primary  - If true, consult config before auto-detection;
 *                   if false (normal), use config only as fallback when
 *                   auto-detection finds nothing.
 * verbose         - If true, narrate detection steps to stdout.
 * out_path        - Buffer to store the resolved sensor path.
 * out_sz          - Size of out_path buffer.
 *
 * Returns true on success, false on failure.
 */
bool get_cpu_temperature_sensor_path(const char *config_path, bool config_primary, bool verbose, char *out_path, size_t out_sz);

/**
 * Scans and prints all CPU-related temperature sensors (thermal zones and hwmon)
 * along with their current values to stdout.
 */
void print_all_cpu_temperatures(void);

/**
 * Scans and prints all temperature sensors exposed by the system
 * (thermal zones and hwmon) along with their current values to stdout.
 */
void print_all_system_temperatures(void);

/**
 * Scans and prints all temperature sensors exposed by the system together
 * with additional metadata when available (e.g. hwmon max/crit thresholds
 * and thermal zone trip points).
 */
void print_all_system_temperatures_detailed(void);

#endif /* CPU_TEMP_H */
