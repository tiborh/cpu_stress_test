CC = gcc
CFLAGS = -O2 -Wall -fstack-protector-strong
LDFLAGS = -lpthread -Wl,-z,relro,-z,now

TARGETS = cpu_stress cpu_cores cpu_temp cpu_id timestamp plot_temp list_temps

all: $(TARGETS)

cpu_temp.o: cpu_temp.c cpu_temp.h
	$(CC) $(CFLAGS) -c cpu_temp.c -o cpu_temp.o

cpu_id.o: cpu_id.c cpu_id.h
	$(CC) $(CFLAGS) -c cpu_id.c -o cpu_id.o

timestamp.o: timestamp.c timestamp.h
	$(CC) $(CFLAGS) -c timestamp.c -o timestamp.o

cpu_stress: cpu_stress.c cpu_temp.o cpu_id.o timestamp.o
	$(CC) $(CFLAGS) cpu_stress.c cpu_temp.o cpu_id.o timestamp.o -o cpu_stress $(LDFLAGS)

cpu_cores: cpu_cores.c
	$(CC) $(CFLAGS) cpu_cores.c -o cpu_cores $(LDFLAGS)

cpu_temp: cpu_temp_tool.c cpu_temp.o cpu_id.o
	$(CC) $(CFLAGS) cpu_temp_tool.c cpu_temp.o cpu_id.o -o cpu_temp $(LDFLAGS)

cpu_id: cpu_id_tool.c cpu_id.o
	$(CC) $(CFLAGS) cpu_id_tool.c cpu_id.o -o cpu_id $(LDFLAGS)

timestamp: timestamp_tool.c timestamp.o
	$(CC) $(CFLAGS) timestamp_tool.c timestamp.o -o timestamp $(LDFLAGS)

plot_temp: plot_temp.c
	$(CC) $(CFLAGS) plot_temp.c -o plot_temp $(LDFLAGS)

list_temps: list_temps_tool.c cpu_temp.o cpu_id.o
	$(CC) $(CFLAGS) list_temps_tool.c cpu_temp.o cpu_id.o -o list_temps $(LDFLAGS)

PREFIX ?= $(HOME)/.local

install: all
	install -d $(PREFIX)/bin
	install -m 755 $(TARGETS) $(PREFIX)/bin/

uninstall:
	cd $(PREFIX)/bin && rm -f $(TARGETS)

check: all
	@echo "=== Smoke tests ==="
	@fail=0; \
	for t in $(TARGETS); do \
		printf "  %-12s " "$$t"; \
		case $$t in \
			cpu_stress) \
				if ./$$t auto 1 math >/dev/null 2>&1; then \
					echo "OK"; \
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
				else \
					echo "FAIL"; fail=1; \
				fi ;; \
		esac; \
	done; \
	echo "=== Done ==="; \
	if [ $$fail -ne 0 ]; then echo "Some tests FAILED"; exit 1; fi

clean:
	rm -f $(TARGETS) *.o
