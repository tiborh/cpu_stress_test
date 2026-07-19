#include "cpu_temp.h"
#include "cpu_id.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#define SYSFS_PATH_MAX 4096

/* ── config loader ────────────────────────────────────────────────────────── */

#define MAX_CONFIG_ENTRIES 64
#define MAX_KEY_LEN        128
#define MAX_VAL_LEN        256

typedef struct { char key[MAX_KEY_LEN]; char val[MAX_VAL_LEN]; } ConfigEntry;
static ConfigEntry cfg[MAX_CONFIG_ENTRIES];
static int cfg_count = 0;
static char loaded_config_path[512] = "";

static void load_config(const char *path) {
    if (loaded_config_path[0] && strcmp(loaded_config_path, path) == 0) return;
    cfg_count = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f) && cfg_count < MAX_CONFIG_ENTRIES) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = p, *v = eq + 1;
        /* trim trailing whitespace from key */
        char *ke = eq - 1;
        while (ke >= k && (*ke == ' ' || *ke == '\t')) *ke-- = '\0';
        /* trim leading/trailing whitespace from value */
        while (*v == ' ' || *v == '\t') v++;
        v[strcspn(v, "\r\n")] = '\0';
        char *ve = v + strlen(v) - 1;
        while (ve > v && (*ve == ' ' || *ve == '\t')) *ve-- = '\0';
        snprintf(cfg[cfg_count].key, MAX_KEY_LEN, "%.127s", k);
        snprintf(cfg[cfg_count].val, MAX_VAL_LEN, "%.255s", v);
        cfg_count++;
    }
    fclose(f);
    snprintf(loaded_config_path, sizeof(loaded_config_path), "%s", path);
}

/* longest-prefix lookup: tokens joined by '.' + ".sensor" suffix */
static const char *config_lookup(const char *slug, bool verbose) {
    /* split slug on '_' into tokens */
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", slug);
    char *tokens[32];
    int n = 0;
    char *t = strtok(buf, "_");
    while (t && n < 32) { tokens[n++] = t; t = strtok(NULL, "_"); }

    for (int len = n; len >= 1; len--) {
        char key[MAX_KEY_LEN] = "";
        for (int i = 0; i < len; i++) {
            if (i) strncat(key, ".", sizeof(key) - strlen(key) - 1);
            strncat(key, tokens[i], sizeof(key) - strlen(key) - 1);
        }
        strncat(key, ".sensor", sizeof(key) - strlen(key) - 1);
        if (verbose) printf("  Lookup     : %-40s", key);
        for (int i = 0; i < cfg_count; i++) {
            if (strcmp(cfg[i].key, key) == 0) {
                if (verbose) printf("→ hit: %s\n", cfg[i].val);
                return cfg[i].val;
            }
        }
        if (verbose) printf("→ miss\n");
    }
    /* try default.sensor */
    if (verbose) printf("  Lookup     : %-40s", "default.sensor");
    for (int i = 0; i < cfg_count; i++) {
        if (strcmp(cfg[i].key, "default.sensor") == 0) {
            if (verbose) printf("→ hit: %s\n", cfg[i].val);
            return cfg[i].val;
        }
    }
    if (verbose) printf("→ miss\n");
    return NULL;
}

/* ── sysfs helpers ────────────────────────────────────────────────────────── */

/* resolve "thermal_zone:<type>" → path, or "k10temp:Tctl" etc → path */
static int resolve_sensor(const char *spec, char *out_path, size_t out_sz, bool verbose) {
    char driver[64], label[64];
    const char *colon = strchr(spec, ':');
    if (!colon) return 0;
    size_t dlen = (size_t)(colon - spec);
    if (dlen >= sizeof(driver)) return 0;
    memcpy(driver, spec, dlen); driver[dlen] = '\0';
    snprintf(label, sizeof(label), "%s", colon + 1);

    if (strcmp(driver, "thermal_zone") == 0) {
        /* scan /sys/class/thermal/thermal_zone* for matching type */
        DIR *dir = opendir("/sys/class/thermal");
        if (!dir) return 0;
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
            char type_path[512];
            snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%s/type", e->d_name);
            FILE *tf = fopen(type_path, "r");
            if (!tf) continue;
            char type_name[64];
            int ok = (fgets(type_name, sizeof(type_name), tf) != NULL);
            fclose(tf);
            if (!ok) continue;
            type_name[strcspn(type_name, "\r\n")] = '\0';
            if (verbose) printf("             : /sys/class/thermal/%s  type=%s\n", e->d_name, type_name);
            if (strcmp(type_name, label) == 0) {
                snprintf(out_path, out_sz, "/sys/class/thermal/%s/temp", e->d_name);
                closedir(dir);
                return 1;
            }
        }
        closedir(dir);
        return 0;
    }

    /* hwmon driver scan */
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return 0;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (strncmp(e->d_name, "hwmon", 5) != 0) continue;
        char name_path[512], name[64];
        snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/%s/name", e->d_name);
        FILE *nf = fopen(name_path, "r");
        if (!nf) continue;
        int ok = (fgets(name, sizeof(name), nf) != NULL);
        fclose(nf);
        if (!ok) continue;
        name[strcspn(name, "\r\n")] = '\0';
        if (verbose) printf("             : /sys/class/hwmon/%s  driver=%s\n", e->d_name, name);
        if (strcmp(name, driver) != 0) continue;
        /* driver matches — find label */
        for (int n = 1; n <= 16; n++) {
            char lpath[512], lname[64];
            snprintf(lpath, sizeof(lpath), "/sys/class/hwmon/%s/temp%d_label", e->d_name, n);
            FILE *lf = fopen(lpath, "r");
            if (!lf) continue;
            ok = (fgets(lname, sizeof(lname), lf) != NULL);
            fclose(lf);
            if (!ok) continue;
            lname[strcspn(lname, "\r\n")] = '\0';
            if (strcmp(lname, label) == 0) {
                snprintf(out_path, out_sz, "/sys/class/hwmon/%s/temp%d_input", e->d_name, n);
                closedir(dir);
                return 1;
            }
        }
        /* no label file — fall back to temp1_input for this driver */
        snprintf(out_path, out_sz, "/sys/class/hwmon/%s/temp1_input", e->d_name);
        closedir(dir);
        return 1;
    }
    closedir(dir);
    return 0;
}

/* ── built-in auto-detection ─────────────────────────────────────────────── */

/* forward declaration (defined later in this file) */
static int read_millidegrees(const char *path, int *milli_out);

/* Reject sentinel / implausible readings (e.g. a disconnected acpitz zone
 * reporting -273.20°C, i.e. absolute zero). Real CPU temperatures live well
 * within this window. */
static int sensor_reading_valid(int milli) {
    return milli > -40000 && milli < 200000;
}

/* Locate a usable temperature input within a hwmon directory. Prefers the
 * canonical CPU labels (Tctl/Tdie/Package id 0) when present, otherwise the
 * first input that yields a plausible reading. Returns 1 on success. */
static int hwmon_find_cpu_input(const char *hwmon_name, char *out_path, size_t out_sz) {
    char fallback[512] = "";
    for (int n = 1; n <= 16; n++) {
        char ipath[512];
        snprintf(ipath, sizeof(ipath), "/sys/class/hwmon/%s/temp%d_input", hwmon_name, n);
        if (access(ipath, F_OK) != 0) continue;
        int milli = 0;
        if (!read_millidegrees(ipath, &milli)) continue;
        if (!sensor_reading_valid(milli)) continue;

        if (fallback[0] == '\0')
            snprintf(fallback, sizeof(fallback), "%s", ipath);

        char lpath[512], label[64] = "";
        snprintf(lpath, sizeof(lpath), "/sys/class/hwmon/%s/temp%d_label", hwmon_name, n);
        FILE *lf = fopen(lpath, "r");
        if (lf) {
            if (fgets(label, sizeof(label), lf)) label[strcspn(label, "\r\n")] = '\0';
            fclose(lf);
        }
        if (strcmp(label, "Tctl") == 0 || strcmp(label, "Tdie") == 0 ||
            strcmp(label, "Package id 0") == 0) {
            snprintf(out_path, out_sz, "%s", ipath);
            return 1;
        }
    }
    if (fallback[0]) {
        snprintf(out_path, out_sz, "%s", fallback);
        return 1;
    }
    return 0;
}

/* Unified ranking across thermal zones and hwmon drivers. Higher is better:
 *   4  x86_pkg_temp thermal zone   (Intel package sensor — canonical)
 *   3  k10temp / coretemp hwmon    (dedicated AMD / Intel CPU drivers)
 *   2  cpu-thermal / *cpu* zone    (SoC / ARM-style CPU zone)
 *   1  acpitz thermal zone         (generic ACPI zone — often inaccurate)
 * Only sensors that return a plausible reading are considered, so a broken
 * zone reporting -273.20°C never wins. */
static int auto_detect(char *out_path, size_t out_sz, bool verbose) {
    int best_rank = -1;
    char best_path[512] = "";

    if (verbose) printf("  Detection  : scanning /sys/class/thermal/thermal_zone* ...\n");

    DIR *dir = opendir("/sys/class/thermal");
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
            char type_path[512];
            snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%s/type", e->d_name);
            FILE *tf = fopen(type_path, "r");
            if (!tf) continue;
            char type_name[128];
            int ok = (fgets(type_name, sizeof(type_name), tf) != NULL);
            fclose(tf);
            if (!ok) continue;
            type_name[strcspn(type_name, "\r\n")] = '\0';

            int rank = -1;
            if (strcmp(type_name, "x86_pkg_temp") == 0)                    rank = 4;
            else if (strcmp(type_name, "cpu-thermal") == 0 || strstr(type_name, "cpu")) rank = 2;
            else if (strcmp(type_name, "acpitz") == 0)                     rank = 1;

            char temp_path[512];
            snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/%s/temp", e->d_name);
            int milli = 0;
            int valid = read_millidegrees(temp_path, &milli) && sensor_reading_valid(milli);

            if (verbose) printf("             :   %s  type=%-16s rank=%d %s\n",
                                e->d_name, type_name, rank,
                                valid ? "(valid)" : "(unreadable/implausible — skipped)");

            if (rank > best_rank && valid) {
                best_rank = rank;
                snprintf(best_path, sizeof(best_path), "%s", temp_path);
            }
        }
        closedir(dir);
    }

    if (verbose) printf("             : scanning /sys/class/hwmon/*/name ...\n");

    DIR *hdir = opendir("/sys/class/hwmon");
    if (hdir) {
        struct dirent *e;
        while ((e = readdir(hdir)) != NULL) {
            if (strncmp(e->d_name, "hwmon", 5) != 0) continue;
            char name_path[512], name[64];
            snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/%s/name", e->d_name);
            FILE *nf = fopen(name_path, "r");
            if (!nf) continue;
            int ok = (fgets(name, sizeof(name), nf) != NULL);
            fclose(nf);
            if (!ok) continue;
            name[strcspn(name, "\r\n")] = '\0';

            int match = (strcmp(name, "k10temp") == 0 || strcmp(name, "coretemp") == 0);
            char input_path[512] = "";
            int valid = match && hwmon_find_cpu_input(e->d_name, input_path, sizeof(input_path));

            if (verbose) printf("             :   %s  driver=%-16s %s\n",
                                e->d_name, name,
                                !match ? "(skip)" : (valid ? "(match, rank=3)" : "(match, no valid input)"));

            if (valid && 3 > best_rank) {
                best_rank = 3;
                snprintf(best_path, sizeof(best_path), "%s", input_path);
            }
        }
        closedir(hdir);
    }

    if (best_rank != -1) {
        snprintf(out_path, out_sz, "%s", best_path);
        if (verbose) printf("             : selected (rank %d): %s\n", best_rank, out_path);
        return 1;
    }
    return 0;
}

/* ── public API ──────────────────────────────────────────────────────────── */

bool get_cpu_temperature_sensor_path(const char *config_path, bool config_primary, bool verbose, char *out_path, size_t out_sz) {
    if (!out_path || out_sz == 0) return false;
    out_path[0] = '\0';

    /* get CPU slug for verbose output and config lookup */
    char slug[128] = "";
    if (config_path || verbose) {
        get_cpu_identifier(slug, sizeof(slug));
    }

    if (verbose) {
        printf("  CPU slug   : %s\n", slug[0] ? slug : "(unknown)");
        printf("  Config     : %s\n", config_path
               ? (config_primary ? config_path : config_path)
               : "not specified (auto mode)");
        if (config_path)
            printf("  Mode       : %s\n", config_primary ? "config-primary" : "auto (config as fallback)");
    }

    if (config_path) load_config(config_path);

    /* ── config-primary: try config first ── */
    if (config_primary && config_path) {
        if (verbose) printf("  Config lookup (primary):\n");
        const char *spec = config_lookup(slug, verbose);
        if (spec) {
            if (verbose) printf("  Resolving  : %s\n", spec);
            if (resolve_sensor(spec, out_path, out_sz, verbose))
                return true;
            if (verbose) printf("  Warning    : could not resolve '%s', falling back to auto\n", spec);
        }
    }

    /* ── auto-detection ── */
    if (out_path[0] == '\0') {
        if (verbose) printf("  Auto-detection:\n");
        if (!auto_detect(out_path, out_sz, verbose)) {
            /* auto failed — try config as fallback */
            if (!config_primary && config_path) {
                if (verbose) printf("  Config lookup (fallback):\n");
                const char *spec = config_lookup(slug, verbose);
                if (spec) {
                    if (verbose) printf("  Resolving  : %s\n", spec);
                    if (resolve_sensor(spec, out_path, out_sz, verbose))
                        return true;
                }
            }
            /* ultimate fallback */
            snprintf(out_path, out_sz, "/sys/class/thermal/thermal_zone0/temp");
            if (verbose) printf("  Fallback   : %s\n", out_path);
        }
    }
    return out_path[0] != '\0';
}

double get_cpu_temperature(const char *config_path, bool config_primary, bool verbose) {
    char sensor_path[512] = "";
    if (!get_cpu_temperature_sensor_path(config_path, config_primary, verbose, sensor_path, sizeof(sensor_path))) {
        return -1.0;
    }

    if (verbose) printf("  Sensor     : %s\n", sensor_path);

    FILE *fp = fopen(sensor_path, "r");
    if (!fp) return -1.0;
    int milli = 0;
    int ok = (fscanf(fp, "%d", &milli) == 1);
    fclose(fp);
    return ok ? milli / 1000.0 : -1.0;
}

static int is_cpu_related_thermal(const char *type_name) {
    return strcmp(type_name, "x86_pkg_temp") == 0 ||
           strcmp(type_name, "cpu-thermal") == 0 ||
           strstr(type_name, "cpu") != NULL ||
           strcmp(type_name, "acpitz") == 0;
}

static int is_cpu_related_hwmon(const char *name) {
    return strcmp(name, "k10temp") == 0 || strcmp(name, "coretemp") == 0;
}

static int read_millidegrees(const char *path, int *milli_out) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    int ok = (fscanf(fp, "%d", milli_out) == 1);
    fclose(fp);
    return ok;
}

static int read_text_line(const char *path, char *buf, size_t buf_sz) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    int ok = (fgets(buf, (int)buf_sz, fp) != NULL);
    fclose(fp);
    if (!ok) return 0;
    buf[strcspn(buf, "\r\n")] = '\0';
    return 1;
}

static void print_optional_temp_file(const char *label, const char *path) {
    int milli = 0;
    if (read_millidegrees(path, &milli)) {
        printf("      %s: %.2f°C\n", label, milli / 1000.0);
    }
}

static void print_thermal_trip_points(const char *zone_name) {
    for (int n = 0; n < 16; n++) {
        char type_path[SYSFS_PATH_MAX], temp_path[SYSFS_PATH_MAX], hyst_path[SYSFS_PATH_MAX], type_name[64];
        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%s/trip_point_%d_type", zone_name, n);
        if (access(type_path, F_OK) != 0) continue;
        if (!read_text_line(type_path, type_name, sizeof(type_name))) continue;

        snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/%s/trip_point_%d_temp", zone_name, n);
        snprintf(hyst_path, sizeof(hyst_path), "/sys/class/thermal/%s/trip_point_%d_hyst", zone_name, n);

        int milli = 0;
        if (read_millidegrees(temp_path, &milli)) {
            printf("      trip[%d] %-8s: %.2f°C", n, type_name, milli / 1000.0);
            int hyst = 0;
            if (read_millidegrees(hyst_path, &hyst)) {
                printf(" (hyst %.2f°C)", hyst / 1000.0);
            }
            printf("\n");
        }
    }
}

static void print_temperatures(int cpu_only, int detailed) {
    int found_thermal = 0;
    int found_hwmon = 0;

    DIR *dir = opendir("/sys/class/thermal");
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (strncmp(e->d_name, "thermal_zone", 12) != 0) continue;

            char type_path[SYSFS_PATH_MAX], type_name[128];
            snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%s/type", e->d_name);
            if (!read_text_line(type_path, type_name, sizeof(type_name))) continue;

            if (cpu_only && !is_cpu_related_thermal(type_name)) continue;

            char temp_path[SYSFS_PATH_MAX];
            snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/%s/temp", e->d_name);
            int milli = 0;
            if (!read_millidegrees(temp_path, &milli)) continue;

            if (!found_thermal) {
                printf("Thermal Zones:\n");
                found_thermal = 1;
            }
            printf("  - %s [%s]: %.2f°C\n", temp_path, type_name, milli / 1000.0);
            if (detailed) {
                print_thermal_trip_points(e->d_name);
            }
        }
        closedir(dir);
    }

    DIR *hdir = opendir("/sys/class/hwmon");
    if (hdir) {
        struct dirent *e;
        while ((e = readdir(hdir)) != NULL) {
            if (strncmp(e->d_name, "hwmon", 5) != 0) continue;

            char name_path[SYSFS_PATH_MAX], name[64];
            snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/%s/name", e->d_name);
            if (!read_text_line(name_path, name, sizeof(name))) continue;

            if (cpu_only && !is_cpu_related_hwmon(name)) continue;

            for (int n = 1; n <= 16; n++) {
                char ipath[SYSFS_PATH_MAX];
                snprintf(ipath, sizeof(ipath), "/sys/class/hwmon/%s/temp%d_input", e->d_name, n);
                if (access(ipath, F_OK) != 0) continue;

                char lpath[SYSFS_PATH_MAX], label[64] = "";
                snprintf(lpath, sizeof(lpath), "/sys/class/hwmon/%s/temp%d_label", e->d_name, n);
                read_text_line(lpath, label, sizeof(label));

                int milli = 0;
                if (!read_millidegrees(ipath, &milli)) continue;

                if (!found_hwmon) {
                    if (found_thermal) printf("\n");
                    printf("Hwmon Sensors:\n");
                    found_hwmon = 1;
                }
                if (label[0]) {
                    printf("  - %s [%s:%s]: %.2f°C\n", ipath, name, label, milli / 1000.0);
                } else {
                    printf("  - %s [%s]: %.2f°C\n", ipath, name, milli / 1000.0);
                }

                if (detailed) {
                    char path[SYSFS_PATH_MAX];
                    snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp%d_max", e->d_name, n);
                    print_optional_temp_file("max ", path);
                    snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp%d_crit", e->d_name, n);
                    print_optional_temp_file("crit", path);
                }
            }
        }
        closedir(hdir);
    }

    if (!found_thermal && !found_hwmon) {
        if (cpu_only) printf("No CPU-related temperature sensors found.\n");
        else          printf("No temperature sensors found.\n");
    }
}

void print_all_cpu_temperatures(void) {
    print_temperatures(1, 0);
}

void print_all_system_temperatures(void) {
    print_temperatures(0, 0);
}

void print_all_system_temperatures_detailed(void) {
    print_temperatures(0, 1);
}
