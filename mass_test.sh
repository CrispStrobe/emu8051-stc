#!/bin/bash
# mass_test.sh — run all third-party firmware through emu8051-stc
# and optionally compare against ucsim-stc.
#
# Usage:
#   ./mass_test.sh                    # load + run only (fast)
#   ./mass_test.sh --diff             # also diff against ucsim (slow)
#   ./mass_test.sh --diff --limit 50  # diff first 50 active images
#
# The corpus at /mnt/volume1/code/stc-research/hex/ is UNLICENSED.
# This script publishes COUNTS and NAMES only, never file contents.

set -e

CORPUS="${CORPUS:-/mnt/volume1/code/stc-research/hex}"
EMU_TRACE="${EMU_TRACE:-./emu_trace}"
UCSIM_TRACE="${UCSIM_TRACE:-/mnt/volume1/code/ucsim-stc/tests/trace.sh}"
FOSC=11059200
SPAN_NS=2000000
DIFF_MODE=0
LIMIT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --diff) DIFF_MODE=1; shift ;;
        --limit) LIMIT=$2; shift 2 ;;
        *) echo "Unknown: $1"; exit 1 ;;
    esac
done

if [ ! -d "$CORPUS" ]; then
    echo "Corpus not found at $CORPUS" >&2
    exit 1
fi

echo "=== emu8051-stc mass firmware test ==="
echo "Corpus: $CORPUS"
echo "Span: ${SPAN_NS} ns ($(echo "scale=1; $SPAN_NS / 1000000" | bc) ms)"
echo ""

total=0; active=0; silent=0; avr=0
agree=0; differ=0; diff_count=0

for hex in $(find "$CORPUS" -name "*.hex" -readable 2>/dev/null | sort); do
    name=$(basename "$hex")
    total=$((total+1))

    emu_out=$(timeout 10 "$EMU_TRACE" -fosc $FOSC -until-ns $SPAN_NS "$hex" 2>/dev/null || true)
    emu_events=$(echo "$emu_out" | grep -cE "SFR|TF" || true)

    if [ "$emu_events" -eq 0 ]; then
        silent=$((silent+1))
        continue
    fi

    active=$((active+1))

    if [ "$DIFF_MODE" -eq 1 ]; then
        [ "$LIMIT" -gt 0 ] && [ "$diff_count" -ge "$LIMIT" ] && continue
        diff_count=$((diff_count+1))

        ucsim_out=$(timeout 60 "$UCSIM_TRACE" -fosc $FOSC -until-ns $SPAN_NS "$hex" 2>/dev/null || true)

        emu_content=$(echo "$emu_out" | grep -E "SFR|TF" | cut -f2,3)
        ucsim_content=$(echo "$ucsim_out" | grep -E "SFR|TF" | cut -f2,3)

        if [ "$emu_content" = "$ucsim_content" ]; then
            agree=$((agree+1))
        else
            differ=$((differ+1))
            echo "  DIFFER: $name (emu=$emu_events, ucsim=$(echo "$ucsim_content" | grep -c . || echo 0))"
        fi
    fi
done

echo ""
echo "=== Results ==="
echo "Total images: $total"
echo "  Active (>=1 SFR/TF event): $active ($(( active * 100 / total ))%)"
echo "  Silent (no events): $silent"

if [ "$DIFF_MODE" -eq 1 ]; then
    echo ""
    echo "Differential comparison:"
    echo "  Compared: $diff_count"
    echo "  Agree: $agree"
    echo "  Differ: $differ"
    if [ "$diff_count" -gt 0 ]; then
        echo "  Agreement rate: $(( agree * 100 / diff_count ))%"
    fi
fi
