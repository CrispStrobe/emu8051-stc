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

#####################################################################
# Rules
#####################################################################
HEADERS := $(wildcard *.h)
SRC := $(filter-out wasm_api.c test_stc12.c test_blink.c, $(wildcard *.c))
OBJ := $(SRC:.c=.o)

%.o: %.c $(HEADERS)
	 $(CC) $(CFLAGS) $(LDFLAGS) -c -o $@ $<

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test_stc12: test_stc12.c core.c opcodes.c disasm.c stc12.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_stc12.c core.c opcodes.c disasm.c stc12.c

test: test_stc12
	./test_stc12

test-wasm: build/emu8051.js
	node test_wasm.mjs

clean:
	-rm -f $(BIN) $(OBJ) test_stc12

.PHONY: clean all test test-wasm

all: $(BIN)
