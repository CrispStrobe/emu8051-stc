#####################################################################
# Build STC12C5A60S2 test images with SDCC
# Usage: make          (builds all test .hex files)
#        make clean    (removes build artifacts)
#####################################################################

SDCC    := sdcc
SFLAGS  := -mmcs51 --model-small --std-sdcc99
SFLAGS  += -I include
SFLAGS  += -DFOSC_HZ=11059200UL

EXAMPLES := 01-blink 02-adc
HEXFILES := $(EXAMPLES:%=%.hex)

all: $(HEXFILES)

01-blink.hex: src/01-blink/main.c include/board.h include/delay.h
	$(SDCC) $(SFLAGS) -o build/01-blink/ src/01-blink/main.c
	cp build/01-blink/main.ihx $@

02-adc.hex: src/02-adc/main.c include/board.h include/delay.h
	$(SDCC) $(SFLAGS) -o build/02-adc/ src/02-adc/main.c
	cp build/02-adc/main.ihx $@

# SDCC needs the output dirs to exist
$(HEXFILES): | builddirs

builddirs:
	mkdir -p build/01-blink build/02-adc

clean:
	rm -rf build $(HEXFILES)

.PHONY: all clean builddirs
