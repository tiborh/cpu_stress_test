#include <stdio.h>
#include <unistd.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [--show-path]\n"
        "  --show-path  : print the source path/interface used to determine the CPU core count\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc > 1) {
        if (strcmp(argv[1], "--show-path") == 0) {
            if (access("/sys/devices/system/cpu/online", R_OK) == 0) {
                printf("/sys/devices/system/cpu/online\n");
            } else if (access("/proc/stat", R_OK) == 0) {
                printf("/proc/stat\n");
            } else {
                printf("sched_getaffinity\n");
            }
            return 0;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 1) {
        perror("Failed to get processor count");
        return 1;
    }
    printf("%ld\n", cores);
    return 0;
}
