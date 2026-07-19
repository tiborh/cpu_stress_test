#include "timestamp.h"
#include <stdio.h>
#include <time.h>

void get_current_timestamp(char *buffer, size_t max_len) {
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    // Format: yyyymmdd_hhmmss (e.g. 20260606_182916)
    strftime(buffer, max_len, "%Y%m%d_%H%M%S", timeinfo);
}
