# Makefile za projekt ARM cache simulacije

# ===============================
# Konfiguracija
# ===============================
# Host kompajler
CC_HOST = gcc
CFLAGS_HOST = -O0 -g

# ARM cross-kompajler
CC_ARM = arm-none-eabi-gcc
CFLAGS_ARM = -mcpu=arm7tdmi -marm -O0 -g -specs=nosys.specs

# Izlazne datoteke
OUT_HOST = simulator
OUT_ARM  = simulator.elf
SRC = src/cache_sim.c

RESULT_DIR = results
TRACE_FILE = traces/trace.txt


.PHONY: all host arm run clean

# ===============================
# Zadaci
# ===============================

all: host arm

# -------------------------------
# Build za host (Linux/WSL)
# -------------------------------
host: $(OUT_HOST)

$(OUT_HOST): $(SRC)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ $<

# -------------------------------
# Cross build za ARM7TDMI
# -------------------------------
arm: $(OUT_ARM)

$(OUT_ARM): $(SRC)
	$(CC_ARM) $(CFLAGS_ARM) -o $@ $<

# -------------------------------
# Pokretanje simulacije (host)
# -------------------------------
run: host
	@mkdir -p $(RESULT_DIR)
ifeq ($(wildcard $(TRACE_FILE)),)
	@echo "Trace datoteka nije pronađena, pokrećem ugrađeni test..."
	./$(OUT_HOST)
else
	@echo "Pokrećem simulaciju s trace datotekom $(TRACE_FILE)..."
	./$(OUT_HOST) --size 16384 --block 16 --assoc 1 --trace $(TRACE_FILE)
endif

# -------------------------------
# Čišćenje
# -------------------------------
clean:
	rm -f $(OUT_HOST) $(OUT_ARM)
	rm -rf $(RESULT_DIR)
run-arm: arm
	@mkdir -p $(RESULT_DIR)
ifeq ($(wildcard $(TRACE_FILE)),)
	@echo "Trace datoteka nije pronađena, pokrećem ugrađeni test na QEMU..."
	qemu-system-arm -M versatilepb -m 128M -nographic \
		-semihosting-config enable=on,target=native \
		-kernel $(OUT_ARM)
else
	@echo "Pokrećem simulaciju na QEMU s trace datotekom $(TRACE_FILE)..."
	qemu-system-arm -M versatilepb -m 128M -nographic \
		-semihosting-config enable=on,target=native \
		-kernel $(OUT_ARM) --size 16384 --block 16 --assoc 1 --trace $(TRACE_FILE)
endif