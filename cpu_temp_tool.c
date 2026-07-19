#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include "cpu_temp.h"

static volatile int keep_running = 1;
static void handle_sigint(int sig) { (void)sig; keep_running = 0; }

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [interval_seconds] [--config <file>] [--config-primary] [--verbose] [--show-path] [--show-all]\n"
        "  interval_seconds  : poll repeatedly (default: print once)\n"
        "  --config <file>   : load sensor hints from config file\n"
        "  --config-primary  : try config before auto-detection (requires --config)\n"
        "  --verbose         : narrate sensor selection steps\n"
        "  --show-path       : print the resolved sensor file path and exit\n"
        "  --show-all        : print all detected CPU-related temperatures and exit\n", prog);
}

int main(int argc, char *argv[]) {
    int interval = 0;
    const char *config_path = NULL;
    bool config_primary = false;
    bool verbose = false;
    bool show_path = false;
    bool show_all = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "--config-primary") == 0) {
            config_primary = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--show-path") == 0) {
            show_path = true;
        } else if (strcmp(argv[i], "--show-all") == 0) {
            show_all = true;
        } else if (argv[i][0] != '-') {
            interval = atoi(argv[i]);
            if (interval < 1) {
                fprintf(stderr, "Error: interval must be a positive integer.\n");
                usage(argv[0]);
                return 1;
            }
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (config_primary && !config_path) {
        fprintf(stderr, "Error: --config-primary requires --config <file>.\n");
        return 1;
    }

    if (show_all) {
        print_all_cpu_temperatures();
        return 0;
    }

    if (show_path) {
        char path[512] = "";
        if (!get_cpu_temperature_sensor_path(config_path, config_primary, verbose, path, sizeof(path))) {
            fprintf(stderr, "Error: could not resolve CPU temperature sensor path.\n");
            return 1;
        }
        if (verbose) {
            printf("  Sensor path: %s\n", path);
        } else {
            printf("%s\n", path);
        }
        return 0;
    }

    if (interval == 0) {
        double temp = get_cpu_temperature(config_path, config_primary, verbose);
        if (temp < 0) {
            fprintf(stderr, "Error: could not read CPU temperature.\n");
            return 1;
        }
        if (verbose) printf("  Temperature: %.2f °C\n", temp);
        else         printf("%.2f°C\n", temp);
    } else {
        signal(SIGINT, handle_sigint);
        printf("Monitoring CPU temperature every %d second(s). Press Ctrl+C to stop.\n", interval);
        while (keep_running) {
            double temp = get_cpu_temperature(config_path, config_primary, verbose);
            if (temp < 0) fprintf(stderr, "Error: could not read CPU temperature.\n");
            else          printf("CPU Temp: %.2f°C\n", temp);
            for (int i = 0; i < interval && keep_running; i++) sleep(1);
        }
        printf("\nMonitoring stopped.\n");
    }
    return 0;
}
