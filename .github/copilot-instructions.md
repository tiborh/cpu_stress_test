# Copilot Instructions

## Build, test, and audit

- Build all Linux C99 utilities with `make`. The project must compile cleanly
  under the Makefile's warnings and hardening flags.
- Build one executable with `make <target>`, for example `make cpu_temp` or
  `make plot_temp`. The targets are `cpu_stress`, `cpu_cores`, `cpu_temp`,
  `cpu_id`, `timestamp`, `plot_temp`, and `list_temps`.
- Run `make test` for hardware-independent `cpu_temp` fixture tests. Run the
  CI smoke suite with `make check`; it runs those tests, performs a one-second
  `math` stress run when sensors are available, and invokes `plot_temp --help`.
- There is no test runner with individual test selectors. For a focused
  behavior check, build the relevant target and invoke its CLI, for example:
  `make cpu_cores && ./cpu_cores`, `make cpu_id && ./cpu_id`, or
  `make plot_temp && ./plot_temp --help`. To exercise one stress workflow,
  run `make cpu_stress && ./cpu_stress auto 1 math`; it writes a CSV in
  `results/`.
- `./test_full.sh --quick` runs the local suite without stress workloads;
  `./test_full.sh` also needs real thermal hardware. CI builds with both
  `gcc` and `clang` on Ubuntu, so use `make clean && make CC=clang` when a
  compiler-specific change needs checking.
- Use `./check_build_deps.sh --build` to verify prerequisites and the full
  build. Run `./check_audit_deps.sh` before `./security_audit.sh`; the latter
  uses cppcheck, flawfinder, valgrind when available, and binary-hardening
  checks.

## Architecture

- This is a Linux-only suite of C command-line programs. It reads CPU details
  from `/proc/cpuinfo` and temperatures from `/sys/class/thermal` and
  `/sys/class/hwmon`; do not introduce assumptions that those interfaces exist
  on other platforms.
- `cpu_temp.c`, `cpu_id.c`, and `timestamp.c` are reusable modules with public
  headers. Their `_tool.c` files are standalone CLI entry points. The Makefile
  links these objects into `cpu_temp`, `cpu_id`, `timestamp`, and `list_temps`;
  `cpu_stress` links all three plus pthread. `cpu_cores` and `plot_temp` are
  standalone programs.
- `cpu_stress` creates worker threads for `math` or `urandom`, samples
  temperatures through `get_cpu_temperature(NULL, false, false)`, and writes
  CSV logs. `plot_temp` reads those logs, validates their schema, groups input
  by CPU ID and method, and invokes gnuplot to generate a PNG.
- Temperature sensor resolution is centralized in `cpu_temp.c`. It ranks valid
  candidates as `x86_pkg_temp`, then `k10temp`/`coretemp`, CPU-named zones,
  then `acpitz`; invalid or implausible readings must not win. `cpu_temp.conf`
  provides optional runtime hints for the `cpu_temp` CLI, while `cpu_stress`
  deliberately uses silent automatic detection only.
- `list_temps` inventories every readable thermal/hwmon value, whereas
  `cpu_temp --show-all` is intentionally limited to CPU-related sensors.

## Repository conventions

- Use C99 with libc and POSIX headers only; pthread is confined to
  `cpu_stress`. Follow the existing `snake_case` function/variable and file
  naming, `UPPER_CASE` macros, and nearby indentation. Report failures to
  stderr and return non-zero status codes.
- Preserve the log contract used by `plot_temp`: files are named
  `results/<cpu_slug>_<method>_<cores>cores_<duration>sec_<timestamp>.csv`
  and begin with
  `Timestamp,ElapsedSeconds,TemperatureCelsius`. CPU slugs are filename-safe;
  timestamps use `YYYYMMDD_HHMMSS`.
- Keep `cpu_temp.conf` entries as `dot.separated.cpu.slug.prefix.sensor =
  driver:label` or `thermal_zone:type`. Lookup is longest matching prefix
  followed by `default.sensor`; resolve drivers and labels dynamically rather
  than hardcoding `hwmonN` paths.
- Synchronize documentation with behavior changes. A changed Makefile target
  belongs in README quick start and `doc/cpu_stress.md`; a new binary also
  needs the utility tables, a dedicated documentation section, `TARGETS`, and
  `.gitignore`. Document script and CLI-option changes in README and/or the
  relevant `doc/cpu_stress.md` section, and config-format changes in its sensor
  configuration section.
- Add notable changes to `doc/CHANGELOG.md` under `## Unreleased` using Keep a
  Changelog categories (`Added`, `Changed`, `Fixed`, or `Removed`).
