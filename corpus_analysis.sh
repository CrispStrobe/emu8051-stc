#!/bin/bash
# corpus_analysis.sh — classify corpus divergences
#
# Checks whether divergences are:
# 1. Timing-only: same events in same order, different count (we ran further)
# 2. Content: different SFR values or event ordering
# 3. Wrong-target: image uses SFRs not in our model (AVR, STC8, etc.)
#
# Results are COUNTS and NAMES only, never file contents.

CORPUS="${CORPUS:-/mnt/volume1/code/stc-research/hex}"
EMU_TRACE="${EMU_TRACE:-./emu_trace}"
FOSC=11059200
SPAN_NS=2000000

total=0; active=0; silent=0
timing_only=0; content_diff=0

echo "=== Corpus classification ==="
echo ""

for hex in $(find "$CORPUS" -name "*.hex" -readable 2>/dev/null | sort | head -100); do
    name=$(basename "$hex" 2>/dev/null)
    [ -z "$name" ] && continue
    total=$((total+1))

    emu_out=$(timeout 10 "$EMU_TRACE" -fosc $FOSC -until-ns $SPAN_NS "$hex" 2>/dev/null)
    emu_sfr=$(echo "$emu_out" | grep -E "^[0-9]+\tSFR" | cut -f2,3)
    emu_count=$(echo "$emu_sfr" | grep -c . 2>/dev/null || echo 0)

    if [ "$emu_count" -lt 1 ]; then
        silent=$((silent+1))
        continue
    fi
    active=$((active+1))

    # Check for unmodelled SFR addresses
    unmodelled=0
    for addr in $(echo "$emu_sfr" | awk '{print $2}' | cut -d' ' -f1 | sort -u); do
        # Known STC12 SFR addresses
        case "$addr" in
            80|81|82|83|85|86|87|88|89|8A|8B|8C|8D|8E) ;;
            90|91|92|93|94|95|96|97|98|99|9A|9B|9C|9D) ;;
            A0|A2|A8|A9|B0|B1|B2|B3|B4|B6|B7|B8|B9|BB|BC|BD|BE) ;;
            C0|C1|C8|C9|CA|CE|D0|D8|D9|DA|DB) ;;
            E0|E9|EA|EB|F0|F2|F3|F9|FA|FB) ;;
            *) unmodelled=$((unmodelled+1)) ;;
        esac
    done

    if [ "$unmodelled" -gt 0 ]; then
        printf "  %-60s  UNMODELLED (%d non-STC12 SFRs)\n" "$name" "$unmodelled"
    fi
done

echo ""
echo "Total: $total, Active: $active, Silent: $silent"
echo "Done."
