# neotype

A matrix-rain typing shooter for the terminal. Letters rain down; type one to
fire at the lowest match. Letters that survive two or three hits change colour
as they weaken. Let one touch the red line above your prompt and the run ends.

```
green = 1 hit    amber = 2 hits    red = 3 hits
```

## Build and run

```
make
./neotype
```

Needs a truecolor terminal: Ghostty, kitty, WezTerm, iTerm2, or similar.
Minimum viewport is 94x26.

## Keys

| Key | Action |
|---|---|
| `a`–`z`, `A`–`Z` | fire at the lowest matching letter |
| Tab | pause |
| Enter | clear the prompt |
| Esc, `q`, Ctrl-C | quit |

Typing is case-sensitive: `a` does not hit `A`. Each shot leaves a mark on
the prompt — `•` hit, `◆` kill, `×` miss — and builds a streak multiplier
that applies at 10 in a row.

## Layout

```
src/
  util.*      rng, math, clock, utf-8          no dependencies
  palette.*   colour language                  no dependencies
  term.*      output buffer, raw mode, signals  POSIX
  canvas.*    cell grid + diff renderer        term, util
  game.*      simulation and rules             util, palette
  draw.*      paints game state to the canvas  canvas, game, palette
  input.*     stdin to key events              term, game
  main.c      wiring and the fixed-step loop
```

Dependencies point one way only. `game` includes neither `canvas.h` nor
`draw.h`, so the simulation never draws and can run without a terminal. That
boundary is what the test harness and the layering check both lean on.

Rendering is a diff against the previous frame, so a frame that changes
nothing emits no bytes.

## Tests

```
make test
```

Two checks:

**Deterministic regression.** The binary can simulate itself with no tty and no
wall clock:

```
NEOTYPE_HEADLESS=1 NEOTYPE_SEED=1 NEOTYPE_W=100 NEOTYPE_H=30 ./neotype
```

It feeds a fixed key script into the sim and prints a digest every 60 frames —
score, level, streak, live drops, RNG state, and a hash of the rendered grid.
`tests/run.sh` runs five stimuli and diffs against `tests/golden.txt`. The set
covers all four states, the retry transition, levels 1 through 7, and both the
smallest and largest supported viewports.

A digest mismatch means the sim, the RNG draw order, or the renderer changed.
If that was deliberate, review the diff and re-record with `make test UPDATE=1`.

**Layering check.** `tests/check-layers.sh` fails if a module includes one from
a higher layer, which keeps the dependency direction from eroding.

## Environment variables

Read only in headless mode, for testing.

| Variable | Default | Meaning |
|---|---|---|
| `NEOTYPE_HEADLESS` | unset | simulate instead of starting the TUI |
| `NEOTYPE_SEED` | time/pid mix | xorshift seed |
| `NEOTYPE_W`, `NEOTYPE_H` | 100, 30 | viewport size |
| `NEOTYPE_FRAMES` | 3600 | frames to simulate |

## License

MIT. See [LICENSE](LICENSE).
