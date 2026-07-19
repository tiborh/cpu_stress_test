/* plot_temp.c — plot cpu_stress CSV temperature logs via gnuplot.
 * Accepts one or more CSV files, or a single directory.
 * See doc/cpu_stress.md §11 for full behaviour documentation. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* Suppress gcc's conservative -Wformat-truncation false positives.
 * All path buffers are bounded by MAX_PATH at the point of writing;
 * gcc infers worst-case sizes from array types rather than actual values. */
#pragma GCC diagnostic ignored "-Wformat-truncation"

#define MAX_FILES   256
#define MAX_ROWS    4096
#define MAX_PATH    1024
#define MAX_PATH_EX 1040  /* MAX_PATH + room for suffixes like ".gp", ".0.dat" */

/* Warning message suffix for mixed CPU/method comparisons — edit here to change wording */
#define APPLES_ORANGES_WARNING ") — Is it possible you are comparing apples with oranges?\n"

/* ── per-file data ─────────────────────────────────────────────────────── */
typedef struct {
    char path[MAX_PATH];
    char cpu_id[128];
    char method[32];
    char timestamp[32];   /* latest timestamp found in filename */
    int  elapsed[MAX_ROWS];
    double temp[MAX_ROWS];
    int  n;               /* row count */
} FileData;

/* ── colour palette ────────────────────────────────────────────────────── */
static const char *COLOURS[] = {
    "#e05050", "#5080e0", "#50c050", "#e0a030",
    "#a050e0", "#30c0c0", "#e050a0", "#808080"
};
#define N_COLOURS (int)(sizeof(COLOURS)/sizeof(COLOURS[0]))

/* ── helpers ───────────────────────────────────────────────────────────── */
static void strip_ext(char *s, const char *ext) {
    size_t sl = strlen(s), el = strlen(ext);
    if (sl > el && strcmp(s + sl - el, ext) == 0) s[sl - el] = '\0';
}

/* Replace underscores with spaces for gnuplot enhanced-text safety */
static void underscores_to_spaces(char *s) {
    for (; *s; s++) if (*s == '_') *s = ' ';
}

/* Parse cpu_id / method / timestamp from basename.
 * Pattern: <cpu_id>_<method>_<N>cores_<M>sec_<timestamp>.csv
 * method is the last simple word before "NNcores". */
static int parse_filename(const char *path, char *cpu_id, size_t cpu_sz,
                          char *method, size_t meth_sz,
                          char *timestamp, size_t ts_sz) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", base);
    strip_ext(tmp, ".csv");

    /* tokenise on '_' */
    char *tokens[64];
    int tc = 0;
    char *p = tmp;
    while (*p && tc < 64) {
        tokens[tc++] = p;
        while (*p && *p != '_') p++;
        if (*p == '_') { *p = '\0'; p++; }
    }
    /* find "NNcores" token */
    int cores_idx = -1;
    for (int i = 0; i < tc; i++) {
        char *t = tokens[i];
        size_t tl = strlen(t);
        if (tl > 5 && strcmp(t + tl - 5, "cores") == 0) { cores_idx = i; break; }
    }
    if (cores_idx < 2) return 0; /* can't parse */

    /* method is the token just before "NNcores" */
    snprintf(method, meth_sz, "%s", tokens[cores_idx - 1]);

    /* cpu_id = tokens[0..cores_idx-2] joined with '_' */
    cpu_id[0] = '\0';
    for (int i = 0; i < cores_idx - 1; i++) {
        if (i > 0) strncat(cpu_id, "_", cpu_sz - strlen(cpu_id) - 1);
        strncat(cpu_id, tokens[i], cpu_sz - strlen(cpu_id) - 1);
    }

    /* timestamp = last token (after "NNsec") */
    snprintf(timestamp, ts_sz, "%s", tokens[tc - 1]);
    return 1;
}

/* Load CSV rows; returns 0 on structural error */
static int load_csv(FileData *fd) {
    FILE *f = fopen(fd->path, "r");
    if (!f) { fprintf(stderr, "Warning: cannot open '%s'\n", fd->path); return 0; }

    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }

    /* validate header */
    line[strcspn(line, "\r\n")] = '\0';
    if (strcmp(line, "Timestamp,ElapsedSeconds,TemperatureCelsius") != 0) {
        fprintf(stderr, "Warning: '%s' has unexpected header — skipping\n", fd->path);
        fclose(f); return 0;
    }

    fd->n = 0;
    while (fgets(line, sizeof(line), f) && fd->n < MAX_ROWS) {
        char ts[32]; double t;
        if (sscanf(line, "%31[^,],%d,%lf", ts, &fd->elapsed[fd->n], &t) == 3) {
            fd->temp[fd->n] = t;
            fd->n++;
        }
    }
    fclose(f);
    if (fd->n == 0) { fprintf(stderr, "Warning: no data rows in '%s'\n", fd->path); return 0; }
    return 1;
}

/* Collect CSV paths from a directory */
static int collect_dir(const char *dir, char paths[][MAX_PATH], int max) {
    DIR *d = opendir(dir);
    if (!d) { perror(dir); return 0; }
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        size_t nl = strlen(e->d_name);
        if (nl < 5 || strcmp(e->d_name + nl - 4, ".csv") != 0) continue;
        snprintf(paths[count++], MAX_PATH, "%s/%s", dir, e->d_name);
    }
    closedir(d);
    return count;
}

/* Longest common prefix of two strings, truncated at last '_' */
static void common_prefix(char *out, size_t sz, const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    size_t len = i < sz ? i : sz - 1;
    if (out != a) { memcpy(out, a, len); }
    out[len] = '\0';
    /* trim trailing underscores / spaces */
    while (len > 0 && (out[len-1] == '_' || out[len-1] == ' ')) out[--len] = '\0';
}

/* Extract CPU model number only (e.g. "j4125" or "7430u") */
static void extract_model_only(const char *cpu_id, char *out, size_t out_sz) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", cpu_id);
    
    char *tokens[32];
    int tc = 0;
    char *p = tmp;
    while (*p && tc < 32) {
        tokens[tc++] = p;
        while (*p && *p != '_') p++;
        if (*p == '_') { *p = '\0'; p++; }
    }
    
    char *best_candidate = NULL;
    int best_score = 0; /* higher is better */
    
    for (int i = 0; i < tc; i++) {
        char *t = tokens[i];
        size_t len = strlen(t);
        if (len == 0) continue;
        
        /* check if it contains frequency (ghz, mhz) */
        char lower[128];
        for (size_t j = 0; j < len && j < 127; j++) {
            lower[j] = (t[j] >= 'A' && t[j] <= 'Z') ? (t[j] + 32) : t[j];
            lower[j+1] = '\0';
        }
        if (strstr(lower, "ghz") || strstr(lower, "mhz")) continue;
        
        /* check digits and letters of token */
        int digits = 0;
        int letters = 0;
        for (size_t j = 0; j < len; j++) {
            if (t[j] >= '0' && t[j] <= '9') digits++;
            else if ((t[j] >= 'a' && t[j] <= 'z') || (t[j] >= 'A' && t[j] <= 'Z')) letters++;
        }
        
        if (digits == 0) continue; /* must have at least one digit */
        
        /* Skip Intel i3/i5/i7/i9/u3/u5/u7/u9 */
        if (len == 2 && (lower[0] == 'i' || lower[0] == 'u') && (lower[1] >= '0' && lower[1] <= '9')) {
            continue;
        }
        
        int score = 0;
        if (digits > 0 && letters > 0) {
            score = 3; /* best: alphanumeric model like j4125, 7430u, n150 */
        } else if (digits >= 3) {
            score = 2; /* good: purely numeric model like 10400, 7763 */
        } else if (digits == 1 && len == 1) {
            score = 1; /* fallback: single digit like 5, 7, 9 */
        }
        
        if (score > best_score) {
            best_score = score;
            best_candidate = t;
        }
    }
    
    if (best_candidate) {
        snprintf(out, out_sz, "%s", best_candidate);
    } else {
        /* ultimate fallback: copy first 10 chars of original cpu_id */
        snprintf(out, out_sz, "%.10s", cpu_id);
    }
}

/* Abbreviate and clean CPU ID (e.g. "int cel j4125" or "amd ryz5 7430u") */
static void abbrev_cpu_id(const char *cpu_id, char *out, size_t out_sz) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", cpu_id);
    
    char *tokens[32];
    int tc = 0;
    char *p = tmp;
    while (*p && tc < 32) {
        tokens[tc++] = p;
        while (*p && *p != '_') p++;
        if (*p == '_') { *p = '\0'; p++; }
    }
    
    char result[256] = "";
    int added = 0;
    char last_abbrev[64] = "";
    
    for (int i = 0; i < tc; i++) {
        char *t = tokens[i];
        size_t len = strlen(t);
        if (len == 0) continue;
        
        /* convert to lower for check */
        char lower[128];
        for (size_t j = 0; j < len && j < 127; j++) {
            lower[j] = (t[j] >= 'A' && t[j] <= 'Z') ? (t[j] + 32) : t[j];
            lower[j+1] = '\0';
        }
        
        /* check noise */
        if (strcmp(lower, "cpu") == 0 || strcmp(lower, "with") == 0 ||
            strcmp(lower, "radeon") == 0 || strcmp(lower, "graphics") == 0 ||
            strcmp(lower, "processor") == 0 || strstr(lower, "ghz") || strstr(lower, "mhz")) {
            continue;
        }
        
        /* check digits and letters of token */
        int digits = 0;
        int letters = 0;
        for (size_t j = 0; j < len; j++) {
            if (t[j] >= '0' && t[j] <= '9') digits++;
            else if ((t[j] >= 'a' && t[j] <= 'z') || (t[j] >= 'A' && t[j] <= 'Z')) letters++;
        }
        
        char abbrev[64] = "";
        if (digits == 1 && len == 1 && added > 0 && strcmp(last_abbrev, "ryz") == 0) {
            /* concatenate single digit to 'ryz' to get e.g. 'ryz5' or 'ryz7' */
            size_t rl = strlen(result);
            if (rl > 0 && result[rl-1] == ' ') {
                result[rl-1] = '\0'; /* remove trailing space of 'ryz ' */
            }
            strncat(result, t, sizeof(result) - strlen(result) - 1);
            strncat(result, " ", sizeof(result) - strlen(result) - 1);
            snprintf(last_abbrev, sizeof(last_abbrev), "%s", t);
            continue;
        }
        
        if (letters > 0 && digits == 0) {
            /* pure alphabetical word */
            if (strcmp(lower, "intel") == 0) {
                snprintf(abbrev, sizeof(abbrev), "int");
            } else if (strcmp(lower, "celeron") == 0) {
                snprintf(abbrev, sizeof(abbrev), "cel");
            } else if (strcmp(lower, "ryzen") == 0) {
                snprintf(abbrev, sizeof(abbrev), "ryz");
            } else if (strcmp(lower, "pentium") == 0) {
                snprintf(abbrev, sizeof(abbrev), "pen");
            } else if (strcmp(lower, "amd") == 0) {
                snprintf(abbrev, sizeof(abbrev), "amd");
            } else {
                /* generic abbreviation of alpha token >= 4 chars to 3 chars */
                if (len >= 4) {
                    snprintf(abbrev, sizeof(abbrev), "%.3s", lower);
                } else {
                    snprintf(abbrev, sizeof(abbrev), "%s", lower);
                }
            }
        } else {
            /* model or other alphanumeric token (e.g. j4125, 7430u, n150) */
            snprintf(abbrev, sizeof(abbrev), "%s", lower);
        }
        
        strncat(result, abbrev, sizeof(result) - strlen(result) - 1);
        strncat(result, " ", sizeof(result) - strlen(result) - 1);
        snprintf(last_abbrev, sizeof(last_abbrev), "%s", abbrev);
        added++;
        
        /* if we just added the model candidate, we can stop to avoid trailing noise */
        if ((digits > 0 && letters > 0) || (digits >= 3)) {
            break;
        }
    }
    
    /* trim trailing space */
    size_t rl = strlen(result);
    if (rl > 0 && result[rl-1] == ' ') result[rl-1] = '\0';
    
    snprintf(out, out_sz, "%s", result[0] ? result : cpu_id);
}

/* ── series: one gnuplot data series (possibly averaged over N files) ─── */
typedef struct {
    char  label[256];
    int   elapsed[MAX_ROWS];
    double temp[MAX_ROWS];
    int   n;
    double sort_key;  /* used for legend ordering */
} Series;

/* Build a series by averaging file group; truncates to shortest duration.
 * Returns 0 on error (start mismatch). */
static int build_averaged_series(FileData *group[], int gc, Series *s, int same_interval) {
    /* verify all start at elapsed 0 */
    for (int i = 0; i < gc; i++) {
        if (group[i]->elapsed[0] != 0) {
            fprintf(stderr,
                "Error: '%s' does not start at elapsed=0 — cannot merge. Aborting.\n",
                group[i]->path);
            return 0;
        }
    }

    if (gc == 1) {
        /* single file: just copy */
        s->n = group[0]->n;
        memcpy(s->elapsed, group[0]->elapsed, s->n * sizeof(int));
        memcpy(s->temp,    group[0]->temp,    s->n * sizeof(double));
        return 1;
    }

    if (!same_interval) {
        /* intersect elapsed sets */
        /* start with set from file 0 */
        int common_e[MAX_ROWS], cn = group[0]->n;
        memcpy(common_e, group[0]->elapsed, cn * sizeof(int));

        for (int i = 1; i < gc; i++) {
            int new_e[MAX_ROWS] = {0}, nn = 0;
            for (int j = 0; j < cn; j++) {
                for (int k = 0; k < group[i]->n; k++) {
                    if (group[i]->elapsed[k] == common_e[j]) { new_e[nn++] = common_e[j]; break; }
                }
            }
            int dropped = cn - nn;
            if (dropped > 0)
                fprintf(stderr, "Warning: %d time point(s) dropped (not common across all files)\n", dropped);
            memcpy(common_e, new_e, nn * sizeof(int)); cn = nn;
        }

        s->n = cn;
        for (int j = 0; j < cn; j++) {
            s->elapsed[j] = common_e[j];
            double sum = 0;
            for (int i = 0; i < gc; i++) {
                for (int k = 0; k < group[i]->n; k++) {
                    if (group[i]->elapsed[k] == common_e[j]) { sum += group[i]->temp[k]; break; }
                }
            }
            s->temp[j] = sum / gc;
        }
    } else {
        /* same interval: truncate to shortest, then average pointwise */
        int min_n = group[0]->n;
        for (int i = 1; i < gc; i++) if (group[i]->n < min_n) min_n = group[i]->n;

        for (int i = 0; i < gc; i++) {
            if (group[i]->n > min_n)
                fprintf(stderr, "Warning: '%s' has %d extra row(s) discarded (truncated to shortest)\n",
                        group[i]->path, group[i]->n - min_n);
        }

        s->n = min_n;
        for (int j = 0; j < min_n; j++) {
            s->elapsed[j] = group[0]->elapsed[j];
            double sum = 0;
            for (int i = 0; i < gc; i++) sum += group[i]->temp[j];
            s->temp[j] = sum / gc;
        }
    }

    printf("Info: averaged %d file(s) into series '%s'\n", gc, s->label);
    return 1;
}

/* ── main ──────────────────────────────────────────────────────────────── */
static void print_usage(const char *prog) {
    printf("Usage: %s <csv_file|dir> [csv_file ...] [options] [output.png]\n", prog);
    printf("Options:\n");
    printf("  --title \"text\"               : custom title for the plot\n");
    printf("  --truncate N                 : truncate CPU ID to N characters in default style (default: 15)\n");
    printf("  --style N / --label-style N  : selection of CPU label style in the plot legend:\n");
    printf("                                   0: truncated full name\n");
    printf("                                   1: abbreviated & clean (e.g., \"int cel j4125\", \"amd ryz5 7430u\") [default]\n");
    printf("                                   2: model number only (e.g., \"j4125\", \"7430u\")\n");
    printf("  --sort MODE                  : legend/series order (descending by temperature):\n");
    printf("                                   last: by final temperature value [default]\n");
    printf("                                   avg:  by mean temperature over the run\n");
    printf("                                   none: preserve input file order\n");
    printf("  -h, --help                   : display this help message\n");
}

int main(int argc, char *argv[]) {
    /* check for help */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
    }

    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* collect input paths */
    static char all_paths[MAX_FILES][MAX_PATH];
    int nfiles = 0;
    char explicit_out[MAX_PATH] = "";
    char explicit_title[256] = "";
    int  cpu_id_max = 15;  /* default truncation length for cpu_id in labels */
    int  label_style = 1;  /* default label style: 1 */
    int  sort_mode = 0;    /* 0=last, 1=avg, 2=none */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--title") == 0 && i + 1 < argc) {
            snprintf(explicit_title, sizeof(explicit_title), "%s", argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--truncate") == 0 && i + 1 < argc) {
            int v = atoi(argv[++i]);
            if (v > 0) cpu_id_max = v;
            continue;
        }
        if ((strcmp(argv[i], "--style") == 0 || strcmp(argv[i], "--label-style") == 0) && i + 1 < argc) {
            label_style = atoi(argv[++i]);
            continue;
        }
        if (strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "avg") == 0)  sort_mode = 1;
            else if (strcmp(m, "none") == 0) sort_mode = 2;
            else sort_mode = 0; /* "last" or anything else */
            continue;
        }
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            /* might be explicit output filename */
            if (i == argc - 1 && argv[i][0] != '-') {
                snprintf(explicit_out, sizeof(explicit_out), "%s", argv[i]);
            } else {
                fprintf(stderr, "Warning: cannot stat '%s' — skipping\n", argv[i]);
            }
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            nfiles += collect_dir(argv[i], all_paths + nfiles, MAX_FILES - nfiles);
        } else {
            size_t nl = strlen(argv[i]);
            if (nl >= 4 && strcmp(argv[i] + nl - 4, ".png") == 0) {
                snprintf(explicit_out, sizeof(explicit_out), "%s", argv[i]);
            } else {
                snprintf(all_paths[nfiles++], MAX_PATH, "%s", argv[i]);
            }
        }
    }

    if (nfiles == 0) { fprintf(stderr, "Error: no input CSV files found\n"); return EXIT_FAILURE; }

    /* load and parse each file */
    static FileData fdata[MAX_FILES];
    int valid = 0;
    for (int i = 0; i < nfiles; i++) {
        snprintf(fdata[valid].path, MAX_PATH, "%s", all_paths[i]);
        if (!parse_filename(all_paths[i],
                            fdata[valid].cpu_id, sizeof(fdata[valid].cpu_id),
                            fdata[valid].method,  sizeof(fdata[valid].method),
                            fdata[valid].timestamp, sizeof(fdata[valid].timestamp))) {
            fprintf(stderr, "Warning: '%s' does not match expected filename pattern — skipping\n", all_paths[i]);
            continue;
        }
        if (!load_csv(&fdata[valid])) continue;
        valid++;
    }

    if (valid == 0) { fprintf(stderr, "Error: no valid CSV files to plot\n"); return EXIT_FAILURE; }

    /* detect distinct cpu_ids and methods */
    char uniq_cpu[MAX_FILES][128]; int n_cpu = 0;
    char uniq_meth[MAX_FILES][32];  int n_meth = 0;
    for (int i = 0; i < valid; i++) {
        int found = 0;
        for (int j = 0; j < n_cpu; j++) if (strcmp(uniq_cpu[j], fdata[i].cpu_id) == 0) { found = 1; break; }
        if (!found) snprintf(uniq_cpu[n_cpu++], 128, "%s", fdata[i].cpu_id);

        found = 0;
        for (int j = 0; j < n_meth; j++) if (strcmp(uniq_meth[j], fdata[i].method) == 0) { found = 1; break; }
        if (!found) snprintf(uniq_meth[n_meth++], 32, "%s", fdata[i].method);
    }

    /* Determine whether there is any overlapping basis for comparison:
     * a method shared by >= 2 distinct CPUs, or a CPU shared by >= 2 distinct
     * methods. If such overlap exists, the comparison is meaningful and the
     * "apples vs oranges" warning is not warranted. Only warn when the inputs
     * are fully disjoint (different CPUs AND different methods with no overlap). */
    int has_overlap = 0;
    for (int a = 0; a < valid && !has_overlap; a++) {
        for (int b = a + 1; b < valid; b++) {
            if (strcmp(fdata[a].cpu_id, fdata[b].cpu_id) == 0 ||
                strcmp(fdata[a].method, fdata[b].method) == 0) {
                has_overlap = 1;
                break;
            }
        }
    }

    if (n_cpu > 1 && n_meth > 1 && !has_overlap) {
        fprintf(stderr, "Warning: mixing different CPUs (");
        for (int i = 0; i < n_cpu; i++) fprintf(stderr, "%s%s", i?", ":"", uniq_cpu[i]);
        fprintf(stderr, ") and different methods (");
        for (int i = 0; i < n_meth; i++) fprintf(stderr, "%s%s", i?", ":"", uniq_meth[i]);
        fprintf(stderr, APPLES_ORANGES_WARNING);
    }

    /* group files by (cpu_id, method) */
    typedef struct { char cpu_id[128]; char method[32]; int idx[MAX_FILES]; int count; } Group;
    static Group groups[MAX_FILES]; int ng = 0;
    for (int i = 0; i < valid; i++) {
        int g = -1;
        for (int j = 0; j < ng; j++) {
            if (strcmp(groups[j].cpu_id, fdata[i].cpu_id) == 0 &&
                strcmp(groups[j].method, fdata[i].method) == 0) { g = j; break; }
        }
        if (g == -1) {
            g = ng++;
            snprintf(groups[g].cpu_id, 128, "%s", fdata[i].cpu_id);
            snprintf(groups[g].method, 32,  "%s", fdata[i].method);
            groups[g].count = 0;
        }
        groups[g].idx[groups[g].count++] = i;
    }

    /* build one Series per group */
    static Series series[MAX_FILES]; int ns = 0;
    for (int g = 0; g < ng; g++) {
        Group *gr = &groups[g];
        Series *s = &series[ns];

        /* determine if all files in group share the same interval */
        int same_interval = 1;
        if (gr->count > 1) {
            int ref_interval = (fdata[gr->idx[0]].n > 1)
                ? fdata[gr->idx[0]].elapsed[1] - fdata[gr->idx[0]].elapsed[0] : 1;
            for (int i = 1; i < gr->count; i++) {
                int iv = (fdata[gr->idx[i]].n > 1)
                    ? fdata[gr->idx[i]].elapsed[1] - fdata[gr->idx[i]].elapsed[0] : 1;
                if (iv != ref_interval) { same_interval = 0; break; }
            }
        }

        /* if same group but different interval → separate series per file */
        if (gr->count > 1 && !same_interval) {
            fprintf(stderr,
                "Warning: files with cpu=%s method=%s have different poll intervals — "
                "plotting as separate series instead of averaging\n",
                gr->cpu_id, gr->method);
            for (int i = 0; i < gr->count; i++) {
                Series *si = &series[ns];
                FileData *fd = &fdata[gr->idx[i]];
                /* label from basename */
                const char *base = strrchr(fd->path, '/'); base = base ? base+1 : fd->path;
                snprintf(si->label, sizeof(si->label), "%s", base);
                strip_ext(si->label, ".csv");
                underscores_to_spaces(si->label);
                si->n = fd->n;
                memcpy(si->elapsed, fd->elapsed, fd->n * sizeof(int));
                memcpy(si->temp,    fd->temp,    fd->n * sizeof(double));
                ns++;
            }
            continue;
        }

        /* build label */
        char cpu_label[256];
        if (label_style == 1 && (int)strlen(gr->cpu_id) > cpu_id_max) {
            abbrev_cpu_id(gr->cpu_id, cpu_label, sizeof(cpu_label));
        } else if (label_style == 2) {
            extract_model_only(gr->cpu_id, cpu_label, sizeof(cpu_label));
        } else {
            /* style 0, or style 1 not needing abbreviation: truncate to cpu_id_max */
            snprintf(cpu_label, sizeof(cpu_label), "%s", gr->cpu_id);
            if ((int)strlen(cpu_label) > cpu_id_max) {
                cpu_label[cpu_id_max] = '\0';
            }
        }
        underscores_to_spaces(cpu_label);
        snprintf(s->label, sizeof(s->label), "%s %s", cpu_label, gr->method);

        FileData *ptrs[MAX_FILES] = {0};
        for (int i = 0; i < gr->count; i++) ptrs[i] = &fdata[gr->idx[i]];

        if (!build_averaged_series(ptrs, gr->count, s, same_interval))
            return EXIT_FAILURE;
        ns++;
    }

    /* compute sort keys and sort series (descending) */
    if (sort_mode != 2 && ns > 1) {
        for (int i = 0; i < ns; i++) {
            Series *s = &series[i];
            if (sort_mode == 1) { /* avg */
                double sum = 0;
                for (int j = 0; j < s->n; j++) sum += s->temp[j];
                s->sort_key = s->n > 0 ? sum / s->n : 0;
            } else { /* last */
                s->sort_key = s->n > 0 ? s->temp[s->n - 1] : 0;
            }
        }
        /* insertion sort (ns is small) */
        for (int i = 1; i < ns; i++) {
            Series tmp = series[i];
            int j = i - 1;
            while (j >= 0 && series[j].sort_key < tmp.sort_key) {
                series[j + 1] = series[j]; j--;
            }
            series[j + 1] = tmp;
        }
    }

    /* determine output PNG path */
    char out_path[MAX_PATH];
    if (explicit_out[0]) {
        snprintf(out_path, sizeof(out_path), "%s", explicit_out);
    } else {
        /* find common directory of all valid input files */
        char common_dir[MAX_PATH] = "";
        for (int i = 0; i < valid; i++) {
            char dir[MAX_PATH]; snprintf(dir, sizeof(dir), "%s", fdata[i].path);
            char *slash = strrchr(dir, '/');
            if (slash) { *slash = '\0'; } else { dir[0] = '.'; dir[1] = '\0'; }
            if (i == 0) {
                snprintf(common_dir, sizeof(common_dir), "%s", dir);
            } else if (strcmp(common_dir, dir) != 0) {
                snprintf(common_dir, sizeof(common_dir), "results");
            }
        }

        /* longest common prefix of all basenames */
        char prefix[MAX_PATH];
        const char *b0 = strrchr(fdata[0].path, '/'); b0 = b0 ? b0+1 : fdata[0].path;
        snprintf(prefix, sizeof(prefix), "%s", b0);
        strip_ext(prefix, ".csv");
        for (int i = 1; i < valid; i++) {
            const char *bi = strrchr(fdata[i].path, '/'); bi = bi ? bi+1 : fdata[i].path;
            char tmp[MAX_PATH]; snprintf(tmp, sizeof(tmp), "%s", bi); strip_ext(tmp, ".csv");
            common_prefix(prefix, sizeof(prefix), prefix, tmp);
        }
        /* append latest timestamp */
        char latest_ts[32] = "";
        for (int i = 0; i < valid; i++)
            if (strcmp(fdata[i].timestamp, latest_ts) > 0)
                snprintf(latest_ts, sizeof(latest_ts), "%s", fdata[i].timestamp);
        /* strip trailing _YYYYMMDD_HHMMSS from prefix if present (15 chars + 1 underscore) */
        size_t pl = strlen(prefix);
        if (pl > 16 && prefix[pl-16] == '_') prefix[pl-16] = '\0';

        const char *stem = prefix[0] ? prefix : "plot";
        /* avoid double slash if common_dir already ends with / */
        size_t dl = strlen(common_dir);
        if (dl > 0 && common_dir[dl-1] == '/') common_dir[dl-1] = '\0';
        snprintf(out_path, sizeof(out_path), "%s/%s_%s.png", common_dir, stem, latest_ts);
    }

    /* find global max temp for y-axis */
    double max_temp = series[0].temp[0];
    for (int i = 0; i < ns; i++)
        for (int j = 0; j < series[i].n; j++)
            if (series[i].temp[j] > max_temp) max_temp = series[i].temp[j];

    /* write one .dat file per series, then write gnuplot script */
    char dat_paths[MAX_FILES][MAX_PATH_EX];
    for (int i = 0; i < ns; i++) {
        printf("Plotted series [%d]: label='%s'\n", i, series[i].label);
        snprintf(dat_paths[i], MAX_PATH_EX, "%s.%d.dat", out_path, i);
        FILE *dat = fopen(dat_paths[i], "w");
        if (!dat) { perror("fopen dat"); return EXIT_FAILURE; }
        for (int j = 0; j < series[i].n; j++)
            fprintf(dat, "%d %.2f\n", series[i].elapsed[j], series[i].temp[j]);
        fclose(dat);
    }

    char gp_path[MAX_PATH_EX];
    snprintf(gp_path, sizeof(gp_path), "%s.gp", out_path);
    FILE *gp = fopen(gp_path, "w");
    if (!gp) { perror("fopen gp"); return EXIT_FAILURE; }

    /* derive plot title */
    char plot_title[MAX_PATH];
    if (explicit_title[0]) {
        snprintf(plot_title, sizeof(plot_title), "%s", explicit_title);
    } else if (valid > 1) {
        snprintf(plot_title, sizeof(plot_title), "CPU Stress Tests");
    } else {
        const char *tb = strrchr(out_path, '/'); tb = tb ? tb+1 : out_path;
        snprintf(plot_title, sizeof(plot_title), "%s", tb);
        strip_ext(plot_title, ".png");
        underscores_to_spaces(plot_title);
    }

    fprintf(gp,
        "set terminal pngcairo size 1200,500 enhanced font 'Sans,11'\n"
        "set output '%s'\n"
        "set title '%s'\n"
        "set xlabel 'Elapsed Time (s)'\n"
        "set ylabel 'CPU Temperature (°C)'\n"
        "set yrange [*:%.0f]\n"
        "set grid\n"
        "set key outside right\n"
        "plot",
        out_path, plot_title, max_temp + 5.0);

    for (int i = 0; i < ns; i++) {
        fprintf(gp, "%s '%s' using 1:2 with linespoints pt 7 ps 0.5 lc rgb '%s' lw 1.5 title '%s'",
                i == 0 ? " " : ", \\\n     ",
                dat_paths[i], COLOURS[i % N_COLOURS], series[i].label);
    }
    fprintf(gp, "\n");
    fclose(gp);

    char cmd[MAX_PATH + 16];
    snprintf(cmd, sizeof(cmd), "gnuplot '%s'", gp_path);
    int rc = system(cmd);

    for (int i = 0; i < ns; i++) remove(dat_paths[i]);
    remove(gp_path);

    if (rc != 0) { fprintf(stderr, "gnuplot failed (exit %d)\n", rc); return EXIT_FAILURE; }
    printf("Plot saved to: %s\n", out_path);
    return EXIT_SUCCESS;
}
