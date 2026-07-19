#!/usr/bin/env bash
# doc_audit.sh — Audit documentation completeness using an AI CLI tool.
#
# Checks that every Makefile target, binary, script, and command-line option
# is properly documented across README.md, doc/cpu_stress.md, and CHANGELOG.md.
#
# Usage:
#   ./doc_audit.sh [--tool kiro|codex|gemini|copilot] [--fix] [--quiet]
#
# Requirements: one of kiro-cli, codex, gemini, or gh (for copilot) must be
# installed and authenticated.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── Defaults ──────────────────────────────────────────────────────────────────
TOOL=""
FIX_MODE=false
QUIET=false

# ── Parse arguments ───────────────────────────────────────────────────────────
usage() {
    echo "Usage: $0 [--tool kiro|codex|gemini|copilot] [--fix] [--quiet]"
    echo ""
    echo "Options:"
    echo "  --tool TOOL   Force a specific AI CLI tool (auto-detected if omitted)"
    echo "  --fix         Ask the AI to produce corrected documentation (not just report)"
    echo "  --quiet       Only output the AI response, no status messages"
    echo "  --help        Show this help"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tool)  TOOL="$2"; shift 2 ;;
        --fix)   FIX_MODE=true; shift ;;
        --quiet) QUIET=true; shift ;;
        --help)  usage ;;
        *)       echo "Unknown option: $1"; usage ;;
    esac
done

# ── Detect available tool ─────────────────────────────────────────────────────
detect_tool() {
    if command -v kiro-cli &>/dev/null; then
        echo "kiro"
    elif command -v codex &>/dev/null; then
        echo "codex"
    elif command -v gemini &>/dev/null; then
        echo "gemini"
    elif command -v gh &>/dev/null && gh copilot --version &>/dev/null 2>&1; then
        echo "copilot"
    else
        echo ""
    fi
}

if [[ -z "$TOOL" ]]; then
    TOOL="$(detect_tool)"
fi

if [[ -z "$TOOL" ]]; then
    echo "ERROR: No supported AI CLI tool found." >&2
    echo "Install one of: kiro-cli, codex, gemini, or gh (with copilot extension)" >&2
    exit 1
fi

[[ "$QUIET" == false ]] && echo "Using tool: $TOOL"

# ── Gather project state ──────────────────────────────────────────────────────
MAKEFILE_TARGETS=$(grep -oP '(?<=^TARGETS = ).*' Makefile | tr ' ' '\n')
SCRIPTS=$(find . -maxdepth 1 -name '*.sh' -printf '%f\n' | sort)
GITIGNORE_CONTENT=$(cat .gitignore)
README_CONTENT=$(cat README.md)
DOC_BUILDING=$(sed -n '/### Building/,/^## /p' doc/cpu_stress.md | head -60)
CHANGELOG_UNRELEASED=$(sed -n '/^## Unreleased/,/^## [0-9]/p' doc/CHANGELOG.md)

# ── Build the audit prompt ────────────────────────────────────────────────────
FIX_INSTRUCTION=""
if [[ "$FIX_MODE" == true ]]; then
    FIX_INSTRUCTION="For each gap found, provide the exact text that should be added or changed, with file path and location."
fi

PROMPT="You are auditing documentation completeness for a C project (cpu_stress_test).

PROJECT STATE:
- Makefile TARGETS: $(echo "$MAKEFILE_TARGETS" | tr '\n' ' ')
- Shell scripts: $(echo "$SCRIPTS" | tr '\n' ' ')
- .gitignore contents:
$GITIGNORE_CONTENT

README.md:
$README_CONTENT

doc/cpu_stress.md Building section (excerpt):
$DOC_BUILDING

CHANGELOG Unreleased section:
$CHANGELOG_UNRELEASED

AUDIT RULES (from .kiro/context.md):
1. Every Makefile target must be documented in README.md and doc/cpu_stress.md
2. Every binary in TARGETS must be listed in README.md 'Utilities' table
3. Every binary in TARGETS must have a section in doc/cpu_stress.md
4. Every binary in TARGETS must be in .gitignore
5. Every script (*.sh) must be mentioned in README.md 'Helper scripts' table
6. CHANGELOG must have entries for unreleased changes
7. Prerequisites table must match actual build/run dependencies

TASK: Check each rule above. Report:
- PASS or FAIL for each rule
- For any FAIL, list what is missing or inconsistent
$FIX_INSTRUCTION

Be concise. Use plain text, no markdown fences."

# ── Run the audit ─────────────────────────────────────────────────────────────
[[ "$QUIET" == false ]] && echo "Running documentation audit..."
[[ "$QUIET" == false ]] && echo "---"

case "$TOOL" in
    kiro)
        echo "$PROMPT" | kiro-cli chat --no-interactive 2>/dev/null || \
        kiro-cli chat "$PROMPT" --no-interactive 2>/dev/null || \
        echo "$PROMPT" | kiro-cli --prompt - 2>/dev/null || {
            # Fallback: write prompt to temp file
            TMPF=$(mktemp /tmp/doc_audit_XXXXXX.txt)
            echo "$PROMPT" > "$TMPF"
            kiro-cli chat < "$TMPF" 2>/dev/null
            rm -f "$TMPF"
        }
        ;;
    codex)
        codex -q "$PROMPT" 2>/dev/null || \
        echo "$PROMPT" | codex --quiet 2>/dev/null || \
        codex --prompt "$PROMPT" 2>/dev/null
        ;;
    gemini)
        gemini "$PROMPT" 2>/dev/null || \
        echo "$PROMPT" | gemini 2>/dev/null
        ;;
    copilot)
        gh copilot suggest "$PROMPT" 2>/dev/null || \
        echo "$PROMPT" | gh copilot explain 2>/dev/null
        ;;
    *)
        echo "ERROR: Unsupported tool: $TOOL" >&2
        exit 1
        ;;
esac

[[ "$QUIET" == false ]] && echo ""
[[ "$QUIET" == false ]] && echo "--- Audit complete ---"
