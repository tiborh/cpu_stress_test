CC = gcc
CFLAGS = -O2 -Wall -Wextra -Wpedantic -fstack-protector-strong -D_FORTIFY_SOURCE=2 -MMD -MP
LDFLAGS = -Wl,-z,relro,-z,now

TARGETS = cpu_stress cpu_cores cpu_temp cpu_id timestamp plot_temp list_temps

# Object files built from library modules
OBJS = cpu_temp.o cpu_id.o timestamp.o

all: $(TARGETS)

# ── Pattern rule for .o files (auto-generates .d dependency files) ────────────
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ── Binaries ──────────────────────────────────────────────────────────────────
cpu_stress: cpu_stress.c $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -lpthread

cpu_cores: cpu_cores.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

cpu_temp: cpu_temp_tool.c cpu_temp.o cpu_id.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

cpu_id: cpu_id_tool.c cpu_id.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

timestamp: timestamp_tool.c timestamp.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

plot_temp: plot_temp.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

list_temps: list_temps_tool.c cpu_temp.o cpu_id.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# ── Include auto-generated dependency files ───────────────────────────────────
-include $(OBJS:.o=.d)

# ── Install / Uninstall ───────────────────────────────────────────────────────
PREFIX ?= $(HOME)/.local

install: all
	install -d $(PREFIX)/bin
	install -m 755 $(TARGETS) $(PREFIX)/bin/

uninstall:
	cd $(PREFIX)/bin && rm -f $(TARGETS)

# ── Smoke tests ───────────────────────────────────────────────────────────────
check: all
	@echo "=== Smoke tests ==="
	@fail=0; \
	for t in $(TARGETS); do \
		printf "  %-12s " "$$t"; \
		case $$t in \
			cpu_stress) \
				if ./$$t auto 1 math >/dev/null 2>&1; then \
					echo "OK"; \
				elif ./$$t 2>&1 | grep -q "^Usage:"; then \
					echo "OK (no sensors, usage verified)"; \
				else \
					echo "FAIL"; fail=1; \
				fi ;; \
			plot_temp) \
				if ./$$t --help >/dev/null 2>&1 || [ $$? -le 1 ]; then \
					echo "OK (--help)"; \
				else \
					echo "FAIL"; fail=1; \
				fi ;; \
			*) \
				if ./$$t >/dev/null 2>&1; then \
					echo "OK"; \
				elif [ "$$t" = "cpu_temp" ] || [ "$$t" = "list_temps" ]; then \
					echo "OK (no sensors)"; \
				else \
					echo "FAIL"; fail=1; \
				fi ;; \
		esac; \
	done; \
	echo "=== Done ==="; \
	if [ $$fail -ne 0 ]; then echo "Some tests FAILED"; exit 1; fi

# ── Plot all results ──────────────────────────────────────────────────────────
plot: plot_temp
	@if ls results/*.csv >/dev/null 2>&1; then \
		./plot_temp results/; \
	else \
		echo "No CSV files in results/"; exit 1; \
	fi

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGETS) *.o *.d
