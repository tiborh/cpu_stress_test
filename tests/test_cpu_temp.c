#define _POSIX_C_SOURCE 200809L

#include "../cpu_temp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect_path(const char *name, const char *root, const char *config_path,
                        bool config_primary, const char *suffix) {
    char actual[512] = "";
    char expected[512];
    snprintf(expected, sizeof(expected), "%s%s", root, suffix);

    if (!get_cpu_temperature_sensor_path(config_path, config_primary, false,
                                         actual, sizeof(actual))) {
        fprintf(stderr, "FAIL: %s did not resolve a sensor path\n", name);
        failures++;
    } else if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n",
                name, expected, actual);
        failures++;
    }
}

static void expect_temperature(const char *root, double expected) {
    double actual = get_cpu_temperature(NULL, false, false);
    if (actual != expected) {
        fprintf(stderr, "FAIL: temperature from %s: expected %.2f, got %.2f\n",
                root, expected, actual);
        failures++;
    }
}

static void use_fixture(const char *root) {
    if (setenv("CPU_TEMP_SYSFS_ROOT", root, 1) != 0) {
        perror("setenv");
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    const char *x86_root = "tests/fixtures/sysfs_x86";
    const char *hwmon_root = "tests/fixtures/sysfs_hwmon";
    const char *fallback_root = "tests/fixtures/sysfs_fallback";
    const char *config_path = "tests/fixtures/cpu_temp_test.conf";

    use_fixture(x86_root);
    expect_path("x86 package temperature wins all lower-ranked candidates",
                x86_root, NULL, false, "/class/thermal/thermal_zone2/temp");
    expect_temperature(x86_root, 65.0);

    use_fixture(hwmon_root);
    expect_path("valid k10temp input beats CPU thermal zone and broken acpitz",
                hwmon_root, NULL, false, "/class/hwmon/hwmon0/temp2_input");
    expect_path("config-primary overrides automatic selection",
                hwmon_root, config_path, true, "/class/thermal/thermal_zone1/temp");
    expect_path("config fallback preserves automatic selection",
                hwmon_root, config_path, false, "/class/hwmon/hwmon0/temp2_input");

    use_fixture(fallback_root);
    expect_path("no usable candidate returns the thermal_zone0 fallback",
                fallback_root, NULL, false, "/class/thermal/thermal_zone0/temp");

    if (failures != 0) {
        fprintf(stderr, "%d cpu_temp test(s) failed\n", failures);
        return EXIT_FAILURE;
    }

    printf("cpu_temp fixture tests passed\n");
    return EXIT_SUCCESS;
}
