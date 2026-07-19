#!/usr/bin/env bash
# doc_audit.sh — Audit documentation completeness using an AI CLI tool.
#
# Checks that every Makefile target, binary, script, and command-line option
# is properly documented across README.md, doc/cpu_stress.md, and CHANGELOG.md.
#
# MODES:
#   (no args)         Dry-run: show gathered project state and detected tool,
#                     but do NOT call the AI.  Safe to run without cost.
#   --run             Call the AI to audit; save report to doc_audit_report.txt.
#   --run --fix       Audit + ask for fix suggestions in one shot.
#   --fix             Read an existing report and ask the AI for fixes only.
#   --fix --report F  Use a custom/edited report file as fix input.
#
# WORKFLOWS:
#   1. Quick fix:     ./doc_audit.sh --run --fix
#   2. Review first:  ./doc_audit.sh --run
#                     (read/edit doc_audit_report.txt)
#                     ./doc_audit.sh --fix
#   3. Interactive:   Open doc_audit_report.txt in an AI chat session (e.g.
#                     kiro-cli chat, codex) and work through fixes conversationally.
#
# OPTIONS:
#   --tool TOOL       Force kiro|codex|gemini|copilot (auto-detected if omitted)
#   --run             Actually invoke the AI (costs tokens/credits)
#   --fix             Generate fix suggestions (from prior report or combined with --run)
#   --report FILE     Use FILE as the audit report for --fix (default: doc_audit_report.txt)
#   --quiet           Suppress status messages
#   --help            Show this help

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── Defaults ──────────────────────────────────────────────────────────────────
TOOL=""
RUN_MODE=false
FIX_MODE=false
QUIET=false
REPORT_FILE="doc_audit_report.txt"

# ── Parse arguments ───────────────────────────────────────────────────────────
usage() {
    sed -n '2,/^$/{ s/^# \?//; p }' "$0"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tool)    TOOL="$2"; shift 2 ;;
        --run)     RUN_MODE=true; shift ;;
        --fix)     FIX_MODE=true; shift ;;
        --report)  REPORT_FILE="$2"; shift 2 ;;
        --quiet)   QUIET=true; shift ;;
        --help|-h) usage ;;
        *)         echo "Unknown option: $1" >&2; usage ;;
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

# ── Gather project state ──────────────────────────────────────────────────────
gather_state() {
    MAKEFILE_TARGETS=$(grep -oP '(?<=^TARGETS = ).*' Makefile | tr ' ' '\n')
    SCRIPTS=$(find . -maxdepth 1 -name '*.sh' -printf '%f\n' | sort)
    GITIGNORE_CONTENT=$(cat .gitignore)
    README_CONTENT=$(cat README.md)
    DOC_BUILDING=$(sed -n '/### Building/,/^## /p' doc/cpu_stress.md 2>/dev/null | head -60 || echo "(section not found)")
    CHANGELOG_UNRELEASED=$(sed -n '/^## Unreleased/,/^## [0-9]/p' doc/CHANGELOG.md 2>/dev/null || echo "(not found)")
}

gather_state

# ── Dry-run mode (default) ────────────────────────────────────────────────────
if [[ "$RUN_MODE" == false && "$FIX_MODE" == false ]]; then
    echo "=== doc_audit.sh — DRY RUN (no AI invoked) ==="
    echo ""
    echo "Detected AI tool: ${TOOL:-NONE}"
    echo ""
    echo "Project state gathered:"
    echo "  Makefile TARGETS: $(echo "$MAKEFILE_TARGETS" | tr '\n' ' ')"
    echo "  Shell scripts:    $(echo "$SCRIPTS" | tr '\n' ' ')"
    echo "  .gitignore lines: $(wc -l < .gitignore)"
    echo ""
    echo "Audit rules (from .kiro/context.md):"
    echo "  1. Every Makefile target documented in README.md + doc/cpu_stress.md"
    echo "  2. Every binary in TARGETS listed in README.md Utilities table"
    echo "  3. Every binary in TARGETS has a section in doc/cpu_stress.md"
    echo "  4. Every binary in TARGETS is in .gitignore"
    echo "  5. Every script (*.sh) in README.md Helper scripts table"
    echo "  6. CHANGELOG has entries for unreleased changes"
    echo "  7. Prerequisites table matches actual dependencies"
    echo ""
    echo "To run the audit (uses AI tokens/credits):"
    echo "  ./doc_audit.sh --run                  # audit only, saves report"
    echo "  ./doc_audit.sh --run --fix            # audit + fix suggestions"
    echo "  ./doc_audit.sh --fix                  # fix from previous report"
    echo "  ./doc_audit.sh --fix --report FILE    # fix from edited report"
    echo ""
    if [[ -f "$REPORT_FILE" ]]; then
        echo "Existing report found: $REPORT_FILE ($(wc -l < "$REPORT_FILE") lines)"
    else
        echo "No existing report file found."
    fi
    exit 0
fi

# ── Validate tool availability ────────────────────────────────────────────────
if [[ -z "$TOOL" ]]; then
    echo "ERROR: No supported AI CLI tool found." >&2
    echo "Install one of: kiro-cli, codex, gemini, or gh (with copilot extension)" >&2
    exit 1
fi

[[ "$QUIET" == false ]] && echo "Using tool: $TOOL"

# ── Build prompts ─────────────────────────────────────────────────────────────
build_audit_prompt() {
    cat <<EOF
You are auditing documentation completeness for a C project (cpu_stress_test).

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

Be concise. Use plain text. Do not use any tools or read any files — all
information needed is provided above. Output only the final audit result.
EOF
}

build_fix_prompt() {
    local report_content="$1"
    cat <<EOF
You are fixing documentation for a C project (cpu_stress_test) based on an
audit report. The project files are in the current directory.

AUDIT REPORT:
$report_content

PROJECT STATE:
- Makefile TARGETS: $(echo "$MAKEFILE_TARGETS" | tr '\n' ' ')
- Shell scripts: $(echo "$SCRIPTS" | tr '\n' ' ')

TASK: For each FAIL item in the audit report, provide the exact corrected text
that should be inserted or replaced. Use this format for each fix:

FILE: <path>
LOCATION: <description of where in the file>
ACTION: <add|replace>
CONTENT:
<the exact text>

Only output fixes for items marked FAIL. Be precise and minimal.
Do not use any tools or read any files — all information needed is provided
above. Output only the fix suggestions.
EOF
}

# ── AI invocation helper ──────────────────────────────────────────────────────
strip_ansi() {
    sed -e 's/\x1b\[[0-9;]*[a-zA-Z]//g' -e 's/\x1b([0-9;]*[a-zA-Z]//g' -e 's/\x0f//g'
}

invoke_ai() {
    local prompt="$1"
    local tmpf
    tmpf=$(mktemp /tmp/doc_audit_XXXXXX.txt)
    echo "$prompt" > "$tmpf"

    local result=""
    case "$TOOL" in
        kiro)
            result=$(kiro-cli chat --no-interactive --trust-tools="" < "$tmpf" 2>/dev/null) || \
            result=$(kiro-cli chat --no-interactive < "$tmpf" 2>/dev/null) || \
            result="ERROR: kiro-cli invocation failed. Try: kiro-cli chat < $tmpf"
            ;;
        codex)
            result=$(codex exec "$(cat "$tmpf")" 2>/dev/null) || \
            result=$(codex -q "$(cat "$tmpf")" 2>/dev/null) || \
            result="ERROR: codex invocation failed."
            ;;
        gemini)
            result=$(gemini -p "$(cat "$tmpf")" 2>/dev/null) || \
            result=$(echo "$(cat "$tmpf")" | gemini -p - 2>/dev/null) || \
            result="ERROR: gemini invocation failed."
            ;;
        copilot)
            result=$(echo "$(cat "$tmpf")" | gh copilot explain 2>/dev/null) || \
            result="ERROR: gh copilot invocation failed."
            ;;
        *)
            result="ERROR: Unsupported tool: $TOOL"
            ;;
    esac

    rm -f "$tmpf"
    # Strip ANSI escape codes for clean text output
    echo "$result" | strip_ansi
}

# ── Run audit ─────────────────────────────────────────────────────────────────
if [[ "$RUN_MODE" == true ]]; then
    [[ "$QUIET" == false ]] && echo "Running documentation audit..."

    AUDIT_PROMPT="$(build_audit_prompt)"
    AUDIT_RESULT="$(invoke_ai "$AUDIT_PROMPT")"

    # Save report
    {
        echo "# Documentation Audit Report"
        echo "# Generated: $(date -Iseconds)"
        echo "# Tool: $TOOL"
        echo "#"
        echo "# To generate fixes: ./doc_audit.sh --fix"
        echo "# To edit and then fix: edit this file, then ./doc_audit.sh --fix"
        echo "# To discuss interactively: open this file in a chat session"
        echo ""
        echo "$AUDIT_RESULT"
    } > "$REPORT_FILE"

    [[ "$QUIET" == false ]] && echo "---"
    echo "$AUDIT_RESULT"
    [[ "$QUIET" == false ]] && echo ""
    [[ "$QUIET" == false ]] && echo "Report saved to: $REPORT_FILE"
fi

# ── Fix mode ──────────────────────────────────────────────────────────────────
if [[ "$FIX_MODE" == true ]]; then
    if [[ "$RUN_MODE" == false ]]; then
        # Fix from existing report
        if [[ ! -f "$REPORT_FILE" ]]; then
            echo "ERROR: No report file found at '$REPORT_FILE'." >&2
            echo "Run './doc_audit.sh --run' first, or specify --report FILE." >&2
            exit 1
        fi
        [[ "$QUIET" == false ]] && echo "Reading report from: $REPORT_FILE"
        AUDIT_RESULT="$(grep -v '^#' "$REPORT_FILE" | sed '/^$/d')"
    fi

    [[ "$QUIET" == false ]] && echo "Generating fix suggestions..."

    FIX_PROMPT="$(build_fix_prompt "$AUDIT_RESULT")"
    FIX_RESULT="$(invoke_ai "$FIX_PROMPT")"

    # Save fix suggestions
    FIX_FILE="${REPORT_FILE%.txt}_fixes.txt"
    {
        echo "# Documentation Fix Suggestions"
        echo "# Generated: $(date -Iseconds)"
        echo "# Based on: $REPORT_FILE"
        echo "# Tool: $TOOL"
        echo "#"
        echo "# Apply these manually, or open in a chat session for interactive editing."
        echo ""
        echo "$FIX_RESULT"
    } > "$FIX_FILE"

    [[ "$QUIET" == false ]] && echo "---"
    echo "$FIX_RESULT"
    [[ "$QUIET" == false ]] && echo ""
    [[ "$QUIET" == false ]] && echo "Fixes saved to: $FIX_FILE"
fi

[[ "$QUIET" == false ]] && echo "--- Done ---"
