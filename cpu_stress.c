/* See doc/cpu_stress.md for design rationale, architecture, and usage documentation. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "cpu_temp.h"
#include "cpu_id.h"
#include "timestamp.h"



// Global flag to control the run state of the threads
volatile bool keep_running = true;

// Stress method using the /dev/urandom mechanism
void* stress_urandom(void* arg) {
    int thread_id = *(int*)arg;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open /dev/urandom");
        return NULL;
    }

    char buffer[4096];
    printf("[Thread %d] Started stressing using /dev/urandom...\n", thread_id);

    while (keep_running) {
        // Read random bytes and discard them (mimicking cat /dev/urandom 1> /dev/null)
        if (read(fd, buffer, sizeof(buffer)) <= 0) {
            break;
        }
    }

    close(fd);
    printf("[Thread %d] Stopped.\n", thread_id);
    return NULL;
}

// Stress method using a CPU-bound mathematical busy-loop
void* stress_math(void* arg) {
    int thread_id = *(int*)arg;
    volatile unsigned long long x = 0;
    
    printf("[Thread %d] Started stressing using busy-loop (math)...\n", thread_id);
    
    while (keep_running) {
        // Simple CPU-bound calculation to keep the core busy in user space
        x = x * 1103515245 + 12345;
    }

    printf("[Thread %d] Stopped.\n", thread_id);
    return NULL;
}

void print_usage(const char* prog_name) {
    printf("Usage: %s <num_cores> <duration_seconds> [method] [poll_interval]\n", prog_name);
    printf("Arguments:\n");
    printf("  <num_cores>        - Number of cores to stress (or 'auto'/'0' to stress all available cores)\n");
    printf("  <duration_seconds> - Stress test duration in seconds\n");
    printf("Methods:\n");
    printf("  urandom  - Stress using /dev/urandom reads (kernel-heavy, default)\n");
    printf("  math     - Stress using CPU busy-loop calculations (user-heavy)\n");
    printf("Optional:\n");
    printf("  [poll_interval]    - Temperature polling frequency in seconds (default: 1)\n");
}

bool is_numeric(const char* str) {
    if (!str || *str == '\0') return false;
    while (*str) {
        if (*str < '0' || *str > '9') return false;
        str++;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 5) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    int num_cores = 0;
    if (strcmp(argv[1], "auto") == 0 || strcmp(argv[1], "all") == 0 || strcmp(argv[1], "0") == 0) {
        long online_cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (online_cores < 1) {
            fprintf(stderr, "Warning: Failed to auto-detect number of cores. Defaulting to 1.\n");
            num_cores = 1;
        } else {
            num_cores = (int)online_cores;
        }
    } else {
        num_cores = atoi(argv[1]);
    }

    int duration = atoi(argv[2]);
    bool use_math = false;
    int poll_interval = 1;

    if (num_cores <= 0) {
        fprintf(stderr, "Error: number of cores must be greater than 0.\n");
        return EXIT_FAILURE;
    }
    if (duration <= 0) {
        fprintf(stderr, "Error: duration must be greater than 0.\n");
        return EXIT_FAILURE;
    }

    if (argc == 4) {
        if (is_numeric(argv[3])) {
            poll_interval = atoi(argv[3]);
        } else {
            if (strcmp(argv[3], "math") == 0) {
                use_math = true;
            } else if (strcmp(argv[3], "urandom") != 0) {
                fprintf(stderr, "Error: Invalid method '%s'.\n", argv[3]);
                print_usage(argv[0]);
                return EXIT_FAILURE;
            }
        }
    } else if (argc == 5) {
        if (strcmp(argv[3], "math") == 0) {
            use_math = true;
        } else if (strcmp(argv[3], "urandom") != 0) {
            fprintf(stderr, "Error: Invalid method '%s'.\n", argv[3]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        poll_interval = atoi(argv[4]);
    }

    if (poll_interval <= 0) {
        fprintf(stderr, "Error: Polling interval must be greater than 0.\n");
        return EXIT_FAILURE;
    }

    printf("Starting stress test on %d cores for %d seconds using '%s' method (polling every %ds)...\n", 
           num_cores, duration, use_math ? "math" : "urandom", poll_interval);

    pthread_t* threads = malloc(num_cores * sizeof(pthread_t));
    int* thread_ids = malloc(num_cores * sizeof(int));
    if (!threads || !thread_ids) {
        perror("Failed to allocate memory");
        return EXIT_FAILURE;
    }

    // Spawn the worker threads
    for (int i = 0; i < num_cores; i++) {
        thread_ids[i] = i;
        int rc;
        if (use_math) {
            rc = pthread_create(&threads[i], NULL, stress_math, &thread_ids[i]);
        } else {
            rc = pthread_create(&threads[i], NULL, stress_urandom, &thread_ids[i]);
        }

        if (rc != 0) {
            perror("Failed to create thread");
            keep_running = false;
            // Join already created threads and cleanup
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            free(threads);
            free(thread_ids);
            return EXIT_FAILURE;
        }
    }

    // Create results directory if it doesn't exist
    if (mkdir("results", 0777) == -1) {
        if (errno != EEXIST) {
            perror("Warning: Failed to create 'results' directory");
        }
    }

    // Retrieve CPU ID and timestamp for filename
    char cpu_id[128];
    char timestamp[32];
    get_cpu_identifier(cpu_id, sizeof(cpu_id));
    get_current_timestamp(timestamp, sizeof(timestamp));

    char csv_filename[512];
    snprintf(csv_filename, sizeof(csv_filename), "results/%s_%s_%dcores_%dsec_%s.csv",
             cpu_id, use_math ? "math" : "urandom", num_cores, duration, timestamp);

    FILE *csv_file = fopen(csv_filename, "w");
    if (!csv_file) {
        fprintf(stderr, "Warning: Failed to open CSV file '%s' for logging: ", csv_filename);
        perror("");
    } else {
        fprintf(csv_file, "Timestamp,ElapsedSeconds,TemperatureCelsius\n");
    }

    double start_temp = get_cpu_temperature(NULL, false, false);
    if (start_temp >= 0.0) {
        printf("Initial CPU Temperature: %.2f°C\n", start_temp);
        if (csv_file) {
            fprintf(csv_file, "%s,0,%.2f\n", timestamp, start_temp);
        }
    }

    // Let the threads run for the user-specified period of time, checking temp periodically
    for (int sec = 0; sec < duration; sec++) {
        sleep(1);
        int elapsed = sec + 1;
        if (elapsed % poll_interval == 0 || elapsed == duration) {
            double temp = get_cpu_temperature(NULL, false, false);
            char loop_ts[32];
            get_current_timestamp(loop_ts, sizeof(loop_ts));

            if (temp >= 0.0) {
                printf("[Monitor] Elapsed: %d/%ds | CPU Temp: %.2f°C\n", elapsed, duration, temp);
                if (csv_file) {
                    fprintf(csv_file, "%s,%d,%.2f\n", loop_ts, elapsed, temp);
                    fflush(csv_file);
                }
            } else {
                printf("[Monitor] Elapsed: %d/%ds\n", elapsed, duration);
                if (csv_file) {
                    fprintf(csv_file, "%s,%d,N/A\n", loop_ts, elapsed);
                    fflush(csv_file);
                }
            }
        }
    }

    // Signal threads to stop running
    printf("Duration elapsed. Stopping threads...\n");
    keep_running = false;

    // Wait for all threads to finish
    for (int i = 0; i < num_cores; i++) {
        pthread_join(threads[i], NULL);
    }

    if (csv_file) {
        fclose(csv_file);
        printf("Temperature logs successfully written to %s\n", csv_filename);
    }

    printf("Stress test complete.\n");

    free(threads);
    free(thread_ids);
    return EXIT_SUCCESS;
}
