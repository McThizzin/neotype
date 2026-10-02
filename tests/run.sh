#!/bin/sh
# Deterministic regression for neotype.
#
# Replays a fixed stimulus against the headless sim for several seeds and
# viewport sizes, then diffs the digest stream against tests/golden.txt.
# Any change to the sim, the RNG draw order, or the renderer shows up here.
#
#   make test                  check against the golden file
#   make test UPDATE=1        re-record the golden file (review the diff!)
set -eu

BIN=./neotype
GOLDEN=tests/golden.txt
ACTUAL=tests/.actual.txt

if [ ! -x "$BIN" ]; then
    echo "tests: $BIN not built; run make first" >&2
    exit 1
fi

# seed  w    h    frames  notes
#   1   100  30   3600    reaches every state incl. game over + retry
#   3   100  30   3600    deep level progression, combo multiplier
#   1337 100 30   3600    game over with zero hits (pure miss path)
#   1   94   26   3600    smallest supported viewport
#   2   200  50   2400    large viewport
CASES="1:100:30:3600 3:100:30:3600 1337:100:30:3600 1:94:26:3600 2:200:50:2400"

: > "$ACTUAL"
echo "$CASES" | tr ' ' '\n' | while IFS=: read -r seed w h frames; do
    [ -n "$seed" ] || continue
    printf '### seed=%s size=%sx%s frames=%s\n' "$seed" "$w" "$h" "$frames" >> "$ACTUAL"
    NEOTYPE_SEED="$seed" NEOTYPE_W="$w" NEOTYPE_H="$h" NEOTYPE_FRAMES="$frames" \
        NEOTYPE_HEADLESS=1 "$BIN" >> "$ACTUAL"
done

if [ "${UPDATE:-0}" = "1" ]; then
    cp "$ACTUAL" "$GOLDEN"
    echo "tests: golden updated ($GOLDEN)"
    exit 0
fi

if [ ! -f "$GOLDEN" ]; then
    echo "tests: no golden file; run 'make test UPDATE=1' to record one" >&2
    exit 1
fi

if diff -u "$GOLDEN" "$ACTUAL"; then
    echo "tests: ok ($(grep -c '^f=' "$ACTUAL") digests match)"
    rm -f "$ACTUAL"
else
    echo "" >&2
    echo "tests: FAILED - sim/render output changed." >&2
    echo "        If the change was intentional, review the diff above and" >&2
    echo "        run 'make test UPDATE=1'." >&2
    exit 1
fi
