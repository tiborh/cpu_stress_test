# Changelog

All notable changes to the **cpu_stress_test** project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project does not (yet) use formal version tags, so entries are grouped by
date. The most recent changes are listed first.

---

## Unreleased

### Changed
- **Dependency checkers now support RPM-based distros.** Both
  `check_build_deps.sh` and `check_audit_deps.sh` recognise a third family,
  `rpm` (Fedora/RHEL via `dnf`/`yum`, openSUSE via `zypper`), in addition to
  Debian (`apt`) and Arch (`pacman`). They print install commands for every
  family, **highlight** the section matching the detected system with a `→`
  marker, and `--install` selects the correct tool (`dnf`/`yum`/`zypper`). Added
  RPM package names (`glibc-devel`, `glibc-static`) and notes about `flawfinder`
  availability on openSUSE. Documentation tables updated accordingly.
  *Validation note:* the RPM paths were validated by simulation (stubbed
  `/etc/os-release` + stand-in `dnf`/`zypper`), not yet on real Fedora/openSUSE
  hosts; this caveat is also recorded in the documentation.

### Added
- Documentation: reorganised docs into `doc/` and added a root `README.md`
  linking the available documentation.

## 2026-06-14

### Added
- **`check_build_deps.sh`** — verifies a machine can build and run the C
  utilities. Detects the distro family (Debian/Ubuntu vs Arch/Manjaro from
  `/etc/os-release`, with an `apt`/`pacman` fallback), checks `gcc`, `make`, and
  the optional runtime tool `gnuplot`, and runs a compile + pthread-link
  self-test using the project's real compiler flags. Prints per-distro install
  commands (and convenient meta-package one-liners) for anything missing.
  Options: `--quiet`, `--build` (run `make` to confirm a full build),
  `--install` (offer to install, asks first), `--help`.
- **`check_audit_deps.sh`** — verifies every package needed by
  `security_audit.sh` is installed. Same distro detection, prints install
  commands for both apt and pacman systems, flags `flawfinder` as AUR-only on
  Arch, and notes the Debian static-libc requirement for the valgrind stage.
  `valgrind` is treated as optional. Options: `--quiet`, `--install`, `--help`.
- New test data: AMD Ryzen 5 (Vega Mobile) `urandom`/`math` 8-core 60s CSV logs
  and a security report (`security_report_20260614_AMD_Ryzen5.txt`).

### Changed
- **CPU temperature auto-detection overhaul (`cpu_temp.c`).** Thermal zones and
  hwmon drivers are now scanned in a single **unified ranking**
  (`x86_pkg_temp` > `k10temp`/`coretemp` > `cpu`-type zones > `acpitz`), and each
  candidate's reading is **validated** before selection. hwmon inputs are matched
  by canonical label (`Tctl`/`Tdie`/`Package id 0`) when available.
- Documentation (`cpu_stress.md`): rewrote the "Sensor Detection Priority",
  multi-zone tie-breaking, and AMD hwmon-notes sections; added a "Building the
  Project" section (prerequisites table + `check_build_deps.sh` usage) and a
  "Checking prerequisites" subsection for `check_audit_deps.sh`.

### Fixed
- **`cpu_temp` failing on AMD systems with a broken `acpitz` zone.** On machines
  such as the AMD Ryzen 5 3500U, the only thermal zone (`acpitz`) reports the
  sentinel **−273.20 °C**. The previous logic preferred thermal zones over
  hwmon and selected that broken zone; the resulting negative value tripped the
  `temp < 0` error check, so `cpu_temp` aborted with "could not read CPU
  temperature" even though `k10temp:Tctl` (~57 °C) was available. The new ranking
  + reading-validation selects `k10temp:Tctl`, matching `sensors`.

### Earlier on 2026-06-14
- Security report and hardening fixes, plus documentation updates.
- RPI4 measurements added.
- ARM CPU identification support; plot label-abbreviation "style 1" made default;
  plot labelling options adjusted; documentation review.
- Intel N110 processor data added.
- `list_temps` utility: inventory of all readable thermal/hwmon sensors
  (with `--details` for thresholds/trip points) and related documentation.

## 2026-06-13
- Added temperature ranking/selection explanation to the documentation.
- `--show-path` option for the CPU-core/temperature source.
- Made the temperature sensor path configurable (`cpu_temp.conf`,
  `--config`/`--config-primary`).
- Measurements for an additional processor type.

## 2026-06-07
- Table of Contents and assorted documentation gap-filling.
- Instructions for adding new processor types.
- Sensor-source read configurability.
- Cleaned up build-time compiler warnings.
- `plot_temp` can aggregate multiple measurement inputs into one chart.
- gnuplot install example; AMD processor adjustments and new data.
- htop bar-colour explanation; "Future Directions" section.

## 2026-06-06
- Initial `cpu_stress` implementation: timed CPU stress with `urandom`
  (kernel-heavy) and `math` (user-heavy) modes.
- Supporting utilities and initial functionality.
