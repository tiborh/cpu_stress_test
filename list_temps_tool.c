#include <stdio.h>
#include <string.h>
#include "cpu_temp.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--details] [--help]\n"
        "  Lists all temperature sensors exposed by the system via thermal zones and hwmon.\n"
        "  --details  Also show extra temperature metadata such as hwmon max/crit\n"
        "             thresholds and thermal-zone trip points when available.\n",
        prog);
}

int main(int argc, char *argv[]) {
    int detailed = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--details") == 0) {
            detailed = 1;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (detailed) print_all_system_temperatures_detailed();
    else          print_all_system_temperatures();
    return 0;
}
