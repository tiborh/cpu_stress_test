# Project Context — cpu_stress_test

## Purpose

This file provides project-level context for AI assistants (Kiro, Copilot,
Codex, Gemini) working on this codebase. It describes the documentation
structure, conventions, and rules that must be maintained.

## Project Overview

A suite of C command-line utilities for CPU stress testing, temperature
monitoring, CPU identification, and result plotting on Linux systems.
License: GPL-3.0.

## Source of Truth

| Aspect | Location |
| :--- | :--- |
| Main technical documentation | `doc/cpu_stress.md` |
| Changelog | `doc/CHANGELOG.md` |
| Quick-start / overview | `README.md` |
| Build system | `Makefile` |
| Sensor config | `cpu_temp.conf` |
| Dependency checkers | `check_build_deps.sh`, `check_audit_deps.sh` |
| Security audit | `security_audit.sh` |
| Coding style & contribution rules | `CONTRIBUTING.md` |

## Documentation Sync Rules

Any change to the following **must** be reflected in documentation:

1. **Makefile targets** — all targets must appear in:
   - `doc/cpu_stress.md` → "Building the Project" section
   - `README.md` → "Quick start" section

2. **New utilities or binaries** — must be added to:
   - `doc/cpu_stress.md` → Utility Overview table + dedicated section
   - `README.md` → "Utilities" table
   - `Makefile` → `TARGETS` variable
   - `.gitignore` → binary name

3. **New scripts** — must be added to:
   - `README.md` → "Helper scripts" table
   - `doc/cpu_stress.md` → relevant section

4. **Command-line options** — any new or changed option must be documented in
   `doc/cpu_stress.md` under the relevant utility section.

5. **Configuration changes** — any change to `cpu_temp.conf` format must be
   documented in `doc/cpu_stress.md` → "Sensor Configuration File" section.

6. **All notable changes** — must be recorded in `doc/CHANGELOG.md` under
   `## Unreleased` following [Keep a Changelog](https://keepachangelog.com/)
   format (Added / Changed / Fixed / Removed).

## Conventions

- **Coding style**: Follow `CONTRIBUTING.md` — indentation, naming, formatting,
  and error-handling rules defined there apply to all generated code.
- **C standard**: C99, compiled with `-O2 -Wall -fstack-protector-strong`
- **Threading**: pthread (used only in `cpu_stress`)
- **Platform**: Linux only (hwmon, thermal_zone, /proc/cpuinfo)
- **Install prefix**: `~/.local` by default, overridable via `PREFIX=`
- **Binary names**: short, lowercase, underscore-separated
- **Result files**: CSV in `results/`, named
  `<cpu_slug>_<mode>_<cores>cores_<duration>sec_<timestamp>.csv`
- **Plots**: PNG files, git-ignored

## Audit Checklist

When reviewing documentation completeness, verify:

- [ ] Every Makefile target is documented in README.md and doc/cpu_stress.md
- [ ] Every binary in TARGETS is listed in README.md "Utilities" table
- [ ] Every binary in TARGETS has a section in doc/cpu_stress.md
- [ ] Every binary in TARGETS is in .gitignore
- [ ] Every script (*.sh) is mentioned in README.md "Helper scripts" table
- [ ] Every command-line option shown by --help is documented
- [ ] CHANGELOG has an entry for any unreleased changes
- [ ] Prerequisites table matches actual build/run dependencies
