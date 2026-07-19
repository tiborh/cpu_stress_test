#include <stdio.h>
#include "timestamp.h"

int main() {
    char ts[32];
    get_current_timestamp(ts, sizeof(ts));
    printf("%s\n", ts);
    return 0;
}
