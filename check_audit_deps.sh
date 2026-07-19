#!/usr/bin/env bash
# check_audit_deps.sh — verify that every package needed by security_audit.sh
# is installed, and tell the user how to install whatever is missing on both
# Debian/Ubuntu (apt), Arch/Manjaro (pacman), and RPM-based systems
# — Fedora/RHEL (dnf) and openSUSE (zypper).
#
# Usage: ./check_audit_deps.sh [--quiet] [--install]
#   --quiet    : only print missing dependencies and the install hint
#   --install  : after reporting, offer to run the install command for the
#                detected distro (asks for confirmation; uses sudo)
#
# Exit code: 0 = all required dependencies present
#            1 = one or more required dependencies missing
#
# Notes:
#   * valgrind is treated as OPTIONAL — security_audit.sh runs fine with
#     --skip-valgrind and degrades gracefully when valgrind is absent.
#   * Static linking for the valgrind stage needs static libc on Debian
#     (libc6-dev); on Arch the static libs ship inside the glibc package.

set -uo pipefail

# ── options ───────────────────────────────────────────────────────────────────
QUIET=0
DO_INSTALL=0
for arg in "$@"; do
    case "$arg" in
        --quiet)   QUIET=1 ;;
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

say()  { [[ $QUIET -eq 1 ]] || echo "$@"; }

# ── detect distribution family ────────────────────────────────────────────────
# FAMILY is one of: debian, arch, rpm, unknown
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
# Fall back to detecting the package manager if os-release was inconclusive.
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
    "bash|bash|bash|bash|1|shell interpreter for the audit script"
    "make|make|make|make|1|drives the build during the audit"
    "gcc|gcc|gcc|gcc|1|compiler + warning checks (also static valgrind builds)"
    "cppcheck|cppcheck|cppcheck|cppcheck|1|static C analysis"
    "flawfinder|flawfinder|flawfinder|flawfinder|1|CWE / dangerous-function scan"
    "readelf|binutils|binutils|binutils|1|binary hardening checks (PIE/RELRO)"
    "objdump|binutils|binutils|binutils|1|stack-canary call counting"
    "awk|gawk|gawk|gawk|1|report parsing"
    "grep|grep|grep|grep|1|report parsing"
    "sed|sed|sed|sed|1|report parsing / help text"
    "date|coreutils|coreutils|coreutils|1|timestamps in the report"
    "valgrind|valgrind|valgrind|valgrind|0|dynamic memory checks (skippable)"
)

# Extra, non-command notes appended to install suggestions when relevant.
DEBIAN_EXTRA_NOTE="libc6-dev (static libc for the valgrind stage; usually preinstalled with gcc)"
RPM_EXTRA_NOTE="glibc-static (static libc needed by the valgrind stage's -static builds)"

# ── checking loop ─────────────────────────────────────────────────────────────
say "${BOLD}security_audit.sh dependency check${RESET}"
say "Detected system : ${CYAN}${DISTRO_NAME}${RESET} (family: ${FAMILY})"
say ""

MISSING_REQUIRED_CMDS=()
MISSING_OPTIONAL_CMDS=()
declare -A MISSING_DEBIAN=()
declare -A MISSING_ARCH=()
declare -A MISSING_RPM=()
REQUIRED_FAIL=0

printf_row() {  # status_colour status_text cmd detail
    [[ $QUIET -eq 1 ]] && return 0
    printf '  %s%-6s%s %-12s %s\n' "$1" "$2" "$RESET" "$3" "$4"
}

for row in "${DEPS[@]}"; do
    IFS='|' read -r cmd deb arch rpm req desc <<< "$row"
    if command -v "$cmd" &>/dev/null; then
        printf_row "$GREEN" "OK" "$cmd" "$(command -v "$cmd")"
    else
        if [[ "$req" == "1" ]]; then
            printf_row "$RED" "MISS" "$cmd" "required — $desc"
            MISSING_REQUIRED_CMDS+=("$cmd")
            REQUIRED_FAIL=1
        else
            printf_row "$YELLOW" "OPT" "$cmd" "optional — $desc"
            MISSING_OPTIONAL_CMDS+=("$cmd")
        fi
        MISSING_DEBIAN["$deb"]=1
        MISSING_ARCH["$arch"]=1
        MISSING_RPM["$rpm"]=1
    fi
done

# ── results ───────────────────────────────────────────────────────────────────
say ""
if [[ ${#MISSING_DEBIAN[@]} -eq 0 ]]; then
    say "${GREEN}${BOLD}All dependencies are installed.${RESET} security_audit.sh is ready to run."
    exit 0
fi

# Build sorted, de-duplicated package lists.
mapfile -t DEB_PKGS < <(printf '%s\n' "${!MISSING_DEBIAN[@]}" | sort -u)
mapfile -t ARCH_PKGS < <(printf '%s\n' "${!MISSING_ARCH[@]}" | sort -u)
mapfile -t RPM_PKGS < <(printf '%s\n' "${!MISSING_RPM[@]}" | sort -u)

if [[ ${#MISSING_REQUIRED_CMDS[@]} -gt 0 ]]; then
    echo "${RED}${BOLD}Missing REQUIRED tools:${RESET} ${MISSING_REQUIRED_CMDS[*]}"
fi
if [[ ${#MISSING_OPTIONAL_CMDS[@]} -gt 0 ]]; then
    echo "${YELLOW}Missing optional tools:${RESET} ${MISSING_OPTIONAL_CMDS[*]}"
    echo "  (the audit still works — run: ./security_audit.sh --skip-valgrind)"
fi
echo ""

# Compose install commands.
DEBIAN_CMD="sudo apt update && sudo apt install -y ${DEB_PKGS[*]}"
ARCH_CMD="sudo pacman -Syu --needed ${ARCH_PKGS[*]}"
DNF_CMD="sudo dnf install -y ${RPM_PKGS[*]}"
ZYPPER_CMD="sudo zypper install -y ${RPM_PKGS[*]}"

# Highlight the section matching the detected system.
hi() { [[ "$FAMILY" == "$1" ]] && printf '%s' "$GREEN$BOLD→ " || printf '  '; }

echo "${BOLD}Install the missing packages${RESET} (your system: ${CYAN}${FAMILY}${RESET}):"
echo ""
echo "$(hi debian)${CYAN}Debian / Ubuntu (apt):${RESET}"
echo "    $DEBIAN_CMD"
echo ""
echo "$(hi arch)${CYAN}Arch / Manjaro (pacman):${RESET}"
echo "    $ARCH_CMD"
echo ""
echo "$(hi rpm)${CYAN}Fedora / RHEL (dnf):${RESET}"
echo "    $DNF_CMD"
echo ""
echo "$(hi rpm)${CYAN}openSUSE (zypper):${RESET}"
echo "    $ZYPPER_CMD"

# flawfinder lives in the AUR on Arch — flag it explicitly.
if printf '%s\n' "${ARCH_PKGS[@]}" | grep -qx "flawfinder"; then
    echo ""
    echo "  ${YELLOW}Note (Arch):${RESET} 'flawfinder' is in the AUR, not the official repos."
    echo "    Use an AUR helper, e.g.:  yay -S flawfinder"
    echo "    or install via pip:        python -m pip install --user flawfinder"
fi

# flawfinder may not be packaged on openSUSE — offer the pip fallback.
if [[ "$FAMILY" == "rpm" ]] && printf '%s\n' "${RPM_PKGS[@]}" | grep -qx "flawfinder"; then
    echo ""
    echo "  ${YELLOW}Note (RPM):${RESET} 'flawfinder' is packaged on Fedora/RHEL but may be"
    echo "    absent on openSUSE. If so, install via pip: python -m pip install --user flawfinder"
fi

# Static-libc reminder for the valgrind stage.
if printf '%s\n' "${DEB_PKGS[@]}" | grep -qx "valgrind"; then
    echo ""
    echo "  ${YELLOW}Note (Debian):${RESET} the valgrind stage statically links test binaries;"
    echo "    ensure static libc is present — ${DEBIAN_EXTRA_NOTE}."
    echo "  ${YELLOW}Note (RPM):${RESET} on Fedora/RHEL/openSUSE also install ${RPM_EXTRA_NOTE}."
fi
echo ""

# ── optional auto-install ─────────────────────────────────────────────────────
if [[ $DO_INSTALL -eq 1 ]]; then
    case "$FAMILY" in
        debian) INSTALL_CMD="$DEBIAN_CMD" ;;
        arch)   INSTALL_CMD="$ARCH_CMD" ;;
        rpm)
            case "$RPM_TOOL" in
                dnf|yum) INSTALL_CMD="sudo $RPM_TOOL install -y ${RPM_PKGS[*]}" ;;
                zypper)  INSTALL_CMD="$ZYPPER_CMD" ;;
                *) echo "${RED}Cannot auto-install: no dnf/yum/zypper found.${RESET}"; exit "$REQUIRED_FAIL" ;;
            esac ;;
        *)      echo "${RED}Cannot auto-install: unsupported distro family.${RESET}"; exit "$REQUIRED_FAIL" ;;
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

# Exit non-zero only when a *required* dependency is missing.
exit "$REQUIRED_FAIL"
