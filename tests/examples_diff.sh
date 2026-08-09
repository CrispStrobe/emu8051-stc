#!/bin/bash
# examples_diff.sh — differential comparison of all stc/examples bundles.
#
# Runs each example through both emulators and compares SFR+TF events.
# Exits non-zero if any differ.
#
# Usage: ./tests/examples_diff.sh [span_ns]
# Default span: 2000000 ns (2 ms)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EMU_TRACE="${EMU_TRACE:-$SCRIPT_DIR/../emu_trace}"
UCSIM_TRACE="${UCSIM_TRACE:-/mnt/volume1/code/ucsim-stc/tests/trace.sh}"
EXAMPLES="${EXAMPLES:-/mnt/volume1/code/stc/examples}"
FOSC=11059200
SPAN="${1:-2000000}"

if [ ! -x "$EMU_TRACE" ]; then
    echo "SKIP: emu_trace not found at $EMU_TRACE" >&2
    echo "  Build with: make emu_trace" >&2
    exit 0
fi

if [ ! -f "$UCSIM_TRACE" ]; then
    echo "SKIP: ucsim trace not found at $UCSIM_TRACE" >&2
    exit 0
fi

pass=0; fail=0; skip=0

echo "=== Example bundle differential comparison ==="
echo "Span: ${SPAN} ns, FOSC: ${FOSC}"
echo ""

for dir in "$EXAMPLES"/[0-9][0-9]-*; do
    name=$(basename "$dir")
    hex="$dir/$name.hex"

    if [ ! -f "$hex" ]; then
        # Try .ihx
        hex="$dir/$name.ihx"
        if [ ! -f "$hex" ]; then
            printf "%-25s SKIP (no hex)\n" "$name"
            skip=$((skip+1))
            continue
        fi
    fi

    # ADC input for dimmer (channel 2 = 512)
    adc_flag=""
    case "$name" in
        *dimmer*) adc_flag="-adc 2,512" ;;
    esac

    # Run both traces
    emu_sfr=$("$EMU_TRACE" -fosc $FOSC -until-ns $SPAN $adc_flag "$hex" 2>/dev/null \
        | grep -E "^[0-9]+\tSFR" | cut -f2,3)
    ucsim_sfr=$("$UCSIM_TRACE" -fosc $FOSC -until-ns $SPAN "$hex" 2>/dev/null \
        | grep -E "^[0-9]+\tSFR" | cut -f2,3)

    emu_n=$(echo "$emu_sfr" | wc -l 2>/dev/null | tr -d ' ')
    ucsim_n=$(echo "$ucsim_sfr" | wc -l 2>/dev/null | tr -d ' ')

    if [ "$emu_n" -eq 0 ] && [ "$ucsim_n" -eq 0 ]; then
        printf "%-25s SKIP (no events)\n" "$name"
        skip=$((skip+1))
        continue
    fi

    if [ "$emu_sfr" = "$ucsim_sfr" ]; then
        printf "%-25s PASS (%d events)\n" "$name" "$emu_n"
        pass=$((pass+1))
    else
        # Check prefix match
        min_n=$((emu_n < ucsim_n ? emu_n : ucsim_n))
        emu_prefix=$(echo "$emu_sfr" | head -$min_n)
        ucsim_prefix=$(echo "$ucsim_sfr" | head -$min_n)
        if [ "$emu_prefix" = "$ucsim_prefix" ]; then
            printf "%-25s PREFIX (emu=%d ucsim=%d, %d shared)\n" "$name" "$emu_n" "$ucsim_n" "$min_n"
        else
            printf "%-25s FAIL (emu=%d ucsim=%d)\n" "$name" "$emu_n" "$ucsim_n"
            diff <(echo "$emu_sfr" | head -5) <(echo "$ucsim_sfr" | head -5) 2>/dev/null | head -5
        fi
        fail=$((fail+1))
    fi
done

echo ""
echo "=== Results ==="
echo "Pass: $pass, Fail/Prefix: $fail, Skip: $skip"

if [ "$fail" -gt 0 ]; then
    exit 1
fi
