#!/usr/bin/env bash
# check_build_deps.sh — verify that everything needed to BUILD and RUN the
# cpu_stress_test C utilities is present, and tell the user how to install
# whatever is missing on Debian/Ubuntu (apt), Arch/Manjaro (pacman), and
# RPM-based systems — Fedora/RHEL (dnf) and openSUSE (zypper).
#
# Usage: ./check_build_deps.sh [--quiet] [--build] [--install]
#   --quiet    : only print missing dependencies and the install hint
#   --build    : after the checks pass, actually run `make` to confirm the
#                whole project compiles (leaves the built binaries in place)
#   --install  : after reporting, offer to run the install command for the
#                detected distro (asks for confirmation; uses sudo)
#
# Exit code: 0 = all required build dependencies present (and, with --build,
#                the project compiled)
#            1 = one or more required dependencies missing / build failed
#
# Notes:
#   * The utilities themselves only use the C standard library, POSIX headers
#     and pthread — there are NO external library dependencies to build them.
#   * gnuplot is an OPTIONAL RUNTIME dependency: only `plot_temp` needs it (it
#     shells out to gnuplot's pngcairo terminal to render PNG charts). All the
#     other tools run without it.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── options ───────────────────────────────────────────────────────────────────
QUIET=0
DO_BUILD=0
DO_INSTALL=0
for arg in "$@"; do
    case "$arg" in
        --quiet)   QUIET=1 ;;
        --build)   DO_BUILD=1 ;;
        --install) DO_INSTALL=1 ;;
        -h|--help)
            sed -n '/^# Usage/,/^#$/p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

# ── colours (only when stdout is a terminal) ──────────────────────────────────
if [[ -t 1 ]]; then
    BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'
    YELLOW=$'\033[33m'; CYAN=$'\033[36m'; RESET=$'\033[0m'
else
    BOLD=""; RED=""; GREEN=""; YELLOW=""; CYAN=""; RESET=""
fi

say() { [[ $QUIET -eq 1 ]] || echo "$@"; }

# ── detect distribution family ────────────────────────────────────────────────
FAMILY="unknown"
DISTRO_NAME="unknown"
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    DISTRO_NAME="${PRETTY_NAME:-${NAME:-unknown}}"
    like="${ID:-} ${ID_LIKE:-}"
    case " $like " in
        *debian*|*ubuntu*)                 FAMILY="debian" ;;
        *arch*|*manjaro*)                  FAMILY="arch" ;;
        *fedora*|*rhel*|*centos*|*suse*|*opensuse*) FAMILY="rpm" ;;
    esac
fi
if [[ "$FAMILY" == "unknown" ]]; then
    if   command -v apt-get &>/dev/null || command -v dpkg &>/dev/null; then FAMILY="debian"
    elif command -v pacman  &>/dev/null; then FAMILY="arch"
    elif command -v dnf &>/dev/null || command -v yum &>/dev/null || command -v zypper &>/dev/null; then FAMILY="rpm"
    fi
fi

# For RPM systems, pick the package manager actually present so --install and
# the highlighted command match the user's system.
RPM_TOOL=""
if   command -v dnf    &>/dev/null; then RPM_TOOL="dnf"
elif command -v yum    &>/dev/null; then RPM_TOOL="yum"
elif command -v zypper &>/dev/null; then RPM_TOOL="zypper"
fi

# ── dependency table ──────────────────────────────────────────────────────────
# Each row: command | debian_pkg | arch_pkg | rpm_pkg | required(1/0) | description
DEPS=(
    "gcc|gcc|gcc|gcc|1|C compiler"
    "make|make|make|make|1|build driver (Makefile)"
    "gnuplot|gnuplot|gnuplot|gnuplot|0|runtime: only plot_temp needs it (PNG charts)"
)

# Package that supplies the C standard library + POSIX headers + pthread.
HDR_PKG_DEBIAN="libc6-dev"   # (pulled in by build-essential)
HDR_PKG_ARCH="glibc"         # (and base-devel for the toolchain)
HDR_PKG_RPM="glibc-devel"    # (and gcc/make, or the development tools group)

say "${BOLD}cpu_stress_test build/run dependency check${RESET}"
say "Detected system : ${CYAN}${DISTRO_NAME}${RESET} (family: ${FAMILY})"
say ""

MISSING_REQUIRED=()
MISSING_OPTIONAL=()
declare -A MISSING_DEBIAN=()
declare -A MISSING_ARCH=()
declare -A MISSING_RPM=()
REQUIRED_FAIL=0

printf_row() {  # colour status cmd detail
    [[ $QUIET -eq 1 ]] && return 0
    printf '  %s%-6s%s %-12s %s\n' "$1" "$2" "$RESET" "$3" "$4"
}

# ── command checks ────────────────────────────────────────────────────────────
for row in "${DEPS[@]}"; do
    IFS='|' read -r cmd deb arch rpm req desc <<< "$row"
    if command -v "$cmd" &>/dev/null; then
        printf_row "$GREEN" "OK" "$cmd" "$(command -v "$cmd")"
    else
        if [[ "$req" == "1" ]]; then
            printf_row "$RED" "MISS" "$cmd" "required — $desc"
            MISSING_REQUIRED+=("$cmd")
            REQUIRED_FAIL=1
        else
            printf_row "$YELLOW" "OPT" "$cmd" "optional — $desc"
            MISSING_OPTIONAL+=("$cmd")
        fi
        MISSING_DEBIAN["$deb"]=1
        MISSING_ARCH["$arch"]=1
        MISSING_RPM["$rpm"]=1
    fi
done

# ── toolchain + headers + pthread compile self-test ───────────────────────────
# This is the most reliable "can it build?" check: it exercises gcc, the C
# standard library / POSIX headers used by the project, the project's compiler
# flags, and pthread linking — all in one go.
HEADERS_OK=1
if command -v gcc &>/dev/null; then
    TMPDIR_T="$(mktemp -d)"
    trap 'rm -rf "$TMPDIR_T"' EXIT
    cat > "$TMPDIR_T/probe.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
static void *worker(void *a) { (void)a; return NULL; }
int main(void) {
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) != 0) return 1;
    pthread_join(t, NULL);
    return 0;
}
EOF
    if gcc -O2 -Wall -fstack-protector-strong "$TMPDIR_T/probe.c" \
           -lpthread -Wl,-z,relro,-z,now -o "$TMPDIR_T/probe" 2>"$TMPDIR_T/err"; then
        printf_row "$GREEN" "OK" "toolchain" "compile + pthread link test passed"
    else
        HEADERS_OK=0
        printf_row "$RED" "MISS" "toolchain" "compile/link test FAILED — C headers or pthread unavailable"
        if [[ $QUIET -eq 0 ]]; then
            sed 's/^/         /' "$TMPDIR_T/err" | head -n 6
        fi
        MISSING_DEBIAN["$HDR_PKG_DEBIAN"]=1
        MISSING_ARCH["$HDR_PKG_ARCH"]=1
        MISSING_RPM["$HDR_PKG_RPM"]=1
        REQUIRED_FAIL=1
    fi
else
    HEADERS_OK=0
    printf_row "$YELLOW" "SKIP" "toolchain" "compile test skipped (gcc missing)"
    MISSING_DEBIAN["$HDR_PKG_DEBIAN"]=1
    MISSING_ARCH["$HDR_PKG_ARCH"]=1
    MISSING_RPM["$HDR_PKG_RPM"]=1
fi

# ── report missing packages ───────────────────────────────────────────────────
say ""
if [[ ${#MISSING_DEBIAN[@]} -gt 0 ]]; then
    mapfile -t DEB_PKGS  < <(printf '%s\n' "${!MISSING_DEBIAN[@]}" | sort -u)
    mapfile -t ARCH_PKGS < <(printf '%s\n' "${!MISSING_ARCH[@]}" | sort -u)
    mapfile -t RPM_PKGS  < <(printf '%s\n' "${!MISSING_RPM[@]}" | sort -u)

    if [[ ${#MISSING_REQUIRED[@]} -gt 0 || $HEADERS_OK -eq 0 ]]; then
        echo "${RED}${BOLD}Missing REQUIRED build dependencies.${RESET}"
    fi
    if [[ ${#MISSING_OPTIONAL[@]} -gt 0 ]]; then
        echo "${YELLOW}Missing optional tools:${RESET} ${MISSING_OPTIONAL[*]}"
        echo "  (only 'plot_temp' needs gnuplot; every other tool builds & runs without it)"
    fi
    echo ""

    DEBIAN_CMD="sudo apt update && sudo apt install -y ${DEB_PKGS[*]}"
    ARCH_CMD="sudo pacman -Syu --needed ${ARCH_PKGS[*]}"
    DNF_CMD="sudo dnf install -y ${RPM_PKGS[*]}"
    ZYPPER_CMD="sudo zypper install -y ${RPM_PKGS[*]}"

    # Highlight the section matching the detected system so the user sees, at a
    # glance, exactly what to run on their own machine.
    hi() { [[ "$FAMILY" == "$1" ]] && printf '%s' "$GREEN$BOLD→ " || printf '  '; }

    echo "${BOLD}Install the missing packages${RESET} (your system: ${CYAN}${FAMILY}${RESET}):"
    echo ""
    echo "$(hi debian)${CYAN}Debian / Ubuntu (apt):${RESET}"
    echo "    $DEBIAN_CMD"
    echo "    ${YELLOW}(or simply: sudo apt install -y build-essential gnuplot)${RESET}"
    echo ""
    echo "$(hi arch)${CYAN}Arch / Manjaro (pacman):${RESET}"
    echo "    $ARCH_CMD"
    echo "    ${YELLOW}(or simply: sudo pacman -S --needed base-devel gnuplot)${RESET}"
    echo ""
    echo "$(hi rpm)${CYAN}Fedora / RHEL (dnf):${RESET}"
    echo "    $DNF_CMD"
    echo "    ${YELLOW}(or simply: sudo dnf groupinstall -y 'Development Tools' && sudo dnf install -y gnuplot)${RESET}"
    echo ""
    echo "$(hi rpm)${CYAN}openSUSE (zypper):${RESET}"
    echo "    $ZYPPER_CMD"
    echo "    ${YELLOW}(or simply: sudo zypper install -t pattern devel_basis && sudo zypper install gnuplot)${RESET}"
    echo ""

    if [[ $DO_INSTALL -eq 1 ]]; then
        case "$FAMILY" in
            debian) INSTALL_CMD="$DEBIAN_CMD" ;;
            arch)   INSTALL_CMD="$ARCH_CMD" ;;
            rpm)
                case "$RPM_TOOL" in
                    dnf|yum) INSTALL_CMD="sudo $RPM_TOOL install -y ${RPM_PKGS[*]}" ;;
                    zypper)  INSTALL_CMD="$ZYPPER_CMD" ;;
                    *) echo "${RED}Cannot auto-install: no dnf/yum/zypper found.${RESET}"; exit 1 ;;
                esac ;;
            *)      echo "${RED}Cannot auto-install: unsupported distro family.${RESET}"; exit 1 ;;
        esac
        echo "About to run for your system (${FAMILY}):"
        echo "    $INSTALL_CMD"
        read -r -p "Proceed? [y/N] " reply
        if [[ "$reply" =~ ^[Yy]$ ]]; then
            eval "$INSTALL_CMD"
        else
            echo "Aborted — no changes made."
        fi
    fi
else
    say "${GREEN}${BOLD}All build/run dependencies are present.${RESET}"
fi

# ── optional real build ───────────────────────────────────────────────────────
if [[ $DO_BUILD -eq 1 ]]; then
    say ""
    if [[ $REQUIRED_FAIL -ne 0 ]]; then
        echo "${RED}Skipping --build: required dependencies are missing.${RESET}"
        exit 1
    fi
    say "${BOLD}Running 'make' to verify the full project builds...${RESET}"
    if make -C "$SCRIPT_DIR" all; then
        say "${GREEN}Build succeeded — all utilities compiled.${RESET}"
    else
        echo "${RED}Build FAILED — see the make output above.${RESET}"
        exit 1
    fi
fi

# Exit non-zero only when a *required* dependency is missing.
exit "$REQUIRED_FAIL"
