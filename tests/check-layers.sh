#!/bin/sh
# Architecture check: each module may only include modules from the layers
# below it. Keeps the dependency direction honest as the game grows.
set -eu

# module:allowed-includes
LAYERS="util:
audio:
palette:util
term:util
canvas:util,term
game:util,palette
draw:util,canvas,game,palette
input:util,term,game
main:util,term,canvas,palette,game,draw,input,audio"

fail=0
echo "$LAYERS" | while IFS=: read -r mod allowed; do
    [ -n "$mod" ] || continue
    src="src/$mod.c"
    [ -f "$src" ] || continue

    # collect local includes, ignoring the module's own header
    got=$(grep -oE '^#include "[a-z]+\.h"' "$src" |
          sed 's/#include "//; s/\.h"//' |
          grep -v "^$mod$" | sort -u | tr '\n' ',' | sed 's/,$//')

    [ -z "$got" ] && continue

    for dep in $(echo "$got" | tr ',' ' '); do
        case ",$allowed," in
            *",$dep,"*) ;;
            *) echo "  VIOLATION: $mod.c includes $dep.h (allowed: ${allowed:-none})" ;;
        esac
    done
done | tee /tmp/.nt_layers
if [ -s /tmp/.nt_layers ]; then
    echo "layering: FAILED" >&2
    exit 1
fi
rm -f /tmp/.nt_layers
echo "layering: ok"
