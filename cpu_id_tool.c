#include <stdio.h>
#include "cpu_id.h"

int main() {
    char cpu_id[128];
    get_cpu_identifier(cpu_id, sizeof(cpu_id));
    printf("%s\n", cpu_id);
    return 0;
}
