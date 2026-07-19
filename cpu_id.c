#include "cpu_id.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/* Decode ARM implementer + part codes into a human-readable name. */
static const char *arm_cpu_name(unsigned int implementer, unsigned int part) {
    if (implementer == 0x41) { /* ARM Ltd */
        switch (part) {
            case 0xd03: return "arm_cortex-a53";
            case 0xd04: return "arm_cortex-a35";
            case 0xd05: return "arm_cortex-a55";
            case 0xd07: return "arm_cortex-a57";
            case 0xd08: return "arm_cortex-a72";
            case 0xd09: return "arm_cortex-a73";
            case 0xd0a: return "arm_cortex-a75";
            case 0xd0b: return "arm_cortex-a76";
            case 0xd0c: return "arm_neoverse-n1";
            case 0xd0d: return "arm_cortex-a77";
            case 0xd41: return "arm_cortex-a78";
            case 0xd44: return "arm_cortex-x1";
            case 0xd46: return "arm_cortex-a510";
            case 0xd47: return "arm_cortex-a710";
            case 0xd48: return "arm_cortex-x2";
        }
    } else if (implementer == 0x51) { /* Qualcomm */
        switch (part) {
            case 0x800: return "qualcomm_kryo-2xx-gold";
            case 0x801: return "qualcomm_kryo-2xx-silver";
            case 0x802: return "qualcomm_kryo-3xx-gold";
            case 0x803: return "qualcomm_kryo-3xx-silver";
            case 0x804: return "qualcomm_kryo-4xx-gold";
            case 0x805: return "qualcomm_kryo-4xx-silver";
        }
    } else if (implementer == 0x53) { /* Samsung */
        switch (part) {
            case 0x001: return "samsung_exynos-m1";
            case 0x002: return "samsung_exynos-m3";
            case 0x004: return "samsung_exynos-m4";
        }
    }
    return NULL;
}

static unsigned int parse_hex_field(FILE *fp, const char *field) {
    rewind(fp);
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, field, strlen(field)) == 0) {
            char *colon = strchr(line, ':');
            if (colon) return (unsigned int)strtoul(colon + 1, NULL, 16);
        }
    }
    return 0;
}

void get_cpu_identifier(char *buffer, size_t max_len) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    char model[128] = "unknown_cpu";
    
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    char *name = colon + 1;
                    while (*name && isspace((unsigned char)*name)) {
                        name++;
                    }
                    size_t len = strlen(name);
                    while (len > 0 && isspace((unsigned char)name[len - 1])) {
                        name[--len] = '\0';
                    }
                    strncpy(model, name, sizeof(model) - 1);
                    model[sizeof(model) - 1] = '\0';
                    break;
                }
            }
        }

        /* ARM fallback: no "model name" field, decode implementer + part */
        if (strcmp(model, "unknown_cpu") == 0) {
            unsigned int impl = parse_hex_field(fp, "CPU implementer");
            unsigned int part = parse_hex_field(fp, "CPU part");
            if (impl || part) {
                const char *name = arm_cpu_name(impl, part);
                if (name) {
                    strncpy(model, name, sizeof(model) - 1);
                    model[sizeof(model) - 1] = '\0';
                } else {
                    snprintf(model, sizeof(model), "arm_impl-0x%02x_part-0x%03x", impl, part);
                }
            }
        }

        fclose(fp);
    }
    
    // Sanitize for filename safety (lowercase, alphanumeric, underscores/hyphens)
    char cleaned[128] = "";
    char *src = model;
    char *dst = cleaned;
    
    while (*src && (dst - cleaned) < (int)(sizeof(cleaned) - 1)) {
        char c = *src;
        if (isalnum((unsigned char)c)) {
            *dst++ = tolower((unsigned char)c);
        } else if (c == '-' || c == '_') {
            *dst++ = c;
        } else if (isspace((unsigned char)c) || c == '(' || c == ')' || c == '@') {
            if (dst > cleaned && *(dst - 1) != '_' && *(dst - 1) != '-') {
                *dst++ = '_';
            }
        }
        src++;
    }
    *dst = '\0';
    
    // Trim trailing separators
    size_t clen = strlen(cleaned);
    while (clen > 0 && (cleaned[clen - 1] == '_' || cleaned[clen - 1] == '-')) {
        cleaned[--clen] = '\0';
    }

    // Collapse multiple consecutive underscores and strip common trademark noise (r, tm)
    char *f_dst = buffer;
    size_t written = 0;
    for (size_t i = 0; i < clen && written < max_len - 1; i++) {
        if (cleaned[i] == '_' && i > 0 && cleaned[i - 1] == '_') {
            continue;
        }
        // Exclude "_r_" if it arose from "(R)"
        if (i >= 2 && cleaned[i] == '_' && cleaned[i-1] == 'r' && cleaned[i-2] == '_') {
            f_dst--; // backtrack
            written--;
            continue;
        }
        // Exclude "_tm_" if it arose from "(TM)"
        if (i >= 3 && cleaned[i] == '_' && cleaned[i-1] == 'm' && cleaned[i-2] == 't' && cleaned[i-3] == '_') {
            f_dst -= 2; // backtrack
            written -= 2;
            continue;
        }
        *f_dst++ = cleaned[i];
        written++;
    }
    *f_dst = '\0';
}
