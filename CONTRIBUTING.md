# Contributing

## Build and test

Before submitting changes, verify everything builds and passes:

```bash
./check_build_deps.sh       # confirm toolchain is complete
make clean && make          # build from scratch — must produce zero warnings
make check                  # smoke-test every binary
```

## Security audit

Run the full static + dynamic audit and confirm no regressions:

```bash
./check_audit_deps.sh       # confirm audit tools are installed
./security_audit.sh         # should produce no new FAIL results
```

## Documentation sync

Any change that adds or modifies a Makefile target, binary, script, or
command-line option must be reflected in the documentation. See
`.kiro/context.md` for the full list of sync rules.

Verify documentation completeness:

```bash
./doc_audit.sh              # dry-run: shows what will be checked
./doc_audit.sh --run        # invoke AI audit (uses tokens/credits)
```

## Changelog

Add an entry under `## Unreleased` in `doc/CHANGELOG.md` for every notable
change, following [Keep a Changelog](https://keepachangelog.com/) format
(Added / Changed / Fixed / Removed).

## Coding style

- **Standard**: C99. No compiler extensions unless unavoidable.
- **Dependencies**: libc + POSIX headers only. pthread is used in `cpu_stress`
  only. No external libraries.
- **Naming**:
  - Functions and variables: `snake_case`
  - Macros and constants: `UPPER_CASE`
  - File names: `snake_case.c` / `snake_case.h`
- **Module structure**: each reusable module has a `.c` + `.h` pair (e.g.
  `cpu_temp.c` / `cpu_temp.h`). Standalone tool entry points use a `_tool.c`
  suffix (e.g. `cpu_temp_tool.c`).
- **Error handling**: print diagnostics to stderr, return non-zero exit codes
  on failure.
- **Warnings**: code must compile with zero warnings under the project's
  CFLAGS (`-Wall -Wextra -Wpedantic`).
- **Formatting**: consistent indentation (spaces or tabs — match the
  surrounding code). Keep lines under 100 characters where practical.

## Commits

- Keep commits focused on a single logical change.
- Write short, imperative commit messages (e.g. "Add make plot target").
- If a commit affects documentation, include the doc update in the same commit.
