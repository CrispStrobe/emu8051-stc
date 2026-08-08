#####################################################################
# Config
#####################################################################
BIN := emu

CFLAGS += -O2
CFLAGS += -pipe
CFLAGS += -g -Wall -Wextra -Wno-unused-parameter -Wshadow

# Uncomment to activate LTO
#CFLAGS += -flto

LDLIBS += -lcurses

# Core files (no curses, no emscripten)
CORE_SRC := core.c opcodes.c disasm.c stc12.c debug.c

#####################################################################
# Rules
#####################################################################
HEADERS := $(wildcard *.h)
SRC := $(filter-out wasm_api.c test_stc12.c test_blink.c test_adc.c test_integration.c test_multi_when.c test_suite.c test_bench.c test_debug.c test_cycles.c test_mass.c trace.c, $(wildcard *.c))
OBJ := $(SRC:.c=.o)

%.o: %.c $(HEADERS)
	 $(CC) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

#####################################################################
# Test images (built with SDCC)
#####################################################################
test-images:
	$(MAKE) -C test_images

#####################################################################
# Tests
#####################################################################
test_stc12: test_stc12.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_stc12.c $(CORE_SRC)

test_blink: test_blink.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_blink.c $(CORE_SRC)

test_adc: test_adc.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_adc.c $(CORE_SRC)

test_integration: test_integration.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_integration.c $(CORE_SRC)

test_multi_when: test_multi_when.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_multi_when.c $(CORE_SRC)

test_suite: test_suite.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_suite.c $(CORE_SRC)

test_debug: test_debug.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_debug.c $(CORE_SRC)

test_cycles: test_cycles.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_cycles.c $(CORE_SRC)

test_mass: test_mass.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_mass.c $(CORE_SRC)

emu_trace: trace.c $(CORE_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ trace.c $(CORE_SRC)

test: test_stc12 test_blink test_adc test_integration test_multi_when test_suite test_debug test_cycles test_mass test-images
	@echo "=== Unit tests ==="
	./test_stc12
	@echo ""
	@echo "=== Firmware test suite ==="
	./test_suite
	@echo ""
	@echo "=== 01-blink integration test ==="
	./test_blink test_images/01-blink.hex
	@echo ""
	@echo "=== 02-adc integration test ==="
	./test_adc test_images/02-adc.hex
	@echo ""
	@echo "=== Extended integration tests ==="
	./test_integration
	@echo ""
	@echo "=== Multi-WHEN cooperative scheduling test ==="
	./test_multi_when test_images/04-multi-when.hex
	@echo ""
	@echo "=== Debug control tests (boundary D) ==="
	./test_debug
	@echo ""
	@echo "=== MCS-51 cycle count verification ==="
	./test_cycles
	@echo ""
	@echo "=== Mass firmware validation ==="
	./test_mass

test-wasm: build/emu8051.js
	node test_wasm.mjs

clean:
	-rm -f $(BIN) $(OBJ) test_stc12 test_blink test_adc test_integration test_multi_when test_suite test_debug test_cycles test_mass emu_trace
	$(MAKE) -C test_images clean

.PHONY: clean all test test-wasm test-images

all: $(BIN)
