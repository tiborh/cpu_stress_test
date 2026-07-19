# cpu_stress_test

A small suite of C command-line utilities for stress-testing CPUs, reading CPU
temperature, identifying the processor, and plotting temperature logs — plus
helper scripts for checking dependencies and running a security audit.

**Platform:** Linux only. The utilities rely on `/proc/cpuinfo`, sysfs thermal
zones (`/sys/class/thermal/`), and hwmon (`/sys/class/hwmon/`) — none of which
exist on macOS, Windows, or BSD.

## Quick start

```bash
make                 # build all utilities
make check           # build + smoke-test every binary
make install         # install to ~/.local/bin (PREFIX= overridable)
make uninstall       # remove from ~/.local/bin
make plot            # re-generate PNG plot from all CSVs in results/
make clean           # delete built binaries and object files
./cpu_stress auto 60 math   # stress all cores for 60s, logging temperature
./cpu_temp           # print current CPU temperature
```

Before building on a fresh machine, you can verify the toolchain and packages:

```bash
./check_build_deps.sh   # build/run prerequisites (gcc, make, gnuplot, …)
./check_audit_deps.sh   # prerequisites for security_audit.sh
```

## Documentation

All project documentation lives in the [`doc/`](doc/) directory:

| Document | Contents |
| :--- | :--- |
| [doc/cpu_stress.md](doc/cpu_stress.md) | **Main documentation** — design, usage, and internals of every utility, build prerequisites, sensor configuration, portability notes, and the security audit. |
| [doc/future_directions.md](doc/future_directions.md) | Ideas for additional stress modes targeting different CPU subsystems. |
| [doc/CHANGELOG.md](doc/CHANGELOG.md) | Dated history of notable changes. |
| [TODO.md](TODO.md) | Immediate tasks, pending work, and in-progress features. |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Workflow, coding style, and commit guidelines for contributors. |

## Utilities

| Utility | Purpose |
| :--- | :--- |
| `cpu_stress` | Run a timed CPU stress workload and log temperature to CSV |
| `cpu_cores` | Print the number of online CPU cores |
| `cpu_temp` | Read the selected CPU temperature sensor once or repeatedly |
| `cpu_id` | Print a sanitized CPU identifier suitable for filenames |
| `timestamp` | Print a filename-safe timestamp |
| `plot_temp` | Plot one or more CSV logs into a PNG chart (needs `gnuplot`) |
| `list_temps` | Inventory all readable thermal/hwmon temperatures on the system |

See [doc/cpu_stress.md](doc/cpu_stress.md) for full per-utility details.

## Helper scripts

| Script | Purpose |
| :--- | :--- |
| `check_build_deps.sh` | Verify build/run prerequisites; per-distro install hints (apt / pacman / dnf / zypper) |
| `check_audit_deps.sh` | Verify `security_audit.sh` prerequisites; per-distro install hints (apt / pacman / dnf / zypper) |
| `security_audit.sh` | Static + dynamic security audit producing a pass/fail/warn report |
| `doc_audit.sh` | AI-powered documentation completeness audit (dry-run by default; `--run` to invoke AI). **Only tested with kiro-cli**; other backends (codex, gemini, copilot) are detected but unvalidated. |
| `test_full.sh` | Full local test suite including hardware-dependent tests (`--quick` to skip stress test) |

## Layout

```
.
├── README.md            # this file
├── doc/                 # documentation (see above)
├── Makefile             # build all utilities
├── *.c / *.h            # sources
├── *.conf               # cpu_temp.conf — sensor hints
├── check_*.sh           # dependency checkers
├── security_audit.sh    # security audit
├── results/             # CSV temperature logs produced by cpu_stress
└── reports/             # saved security-audit reports
```
