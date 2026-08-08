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
CORE_SRC := core.c opcodes.c disasm.c stc12.c

#####################################################################
# Rules
#####################################################################
HEADERS := $(wildcard *.h)
SRC := $(filter-out wasm_api.c test_stc12.c test_blink.c test_adc.c test_integration.c, $(wildcard *.c))
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

test: test_stc12 test_blink test_adc test_integration test-images
	@echo "=== Unit tests ==="
	./test_stc12
	@echo ""
	@echo "=== 01-blink integration test ==="
	./test_blink test_images/01-blink.hex
	@echo ""
	@echo "=== 02-adc integration test ==="
	./test_adc test_images/02-adc.hex
	@echo ""
	@echo "=== Extended integration tests ==="
	./test_integration

test-wasm: build/emu8051.js
	node test_wasm.mjs

clean:
	-rm -f $(BIN) $(OBJ) test_stc12 test_blink test_adc test_integration
	$(MAKE) -C test_images clean

.PHONY: clean all test test-wasm test-images

all: $(BIN)
