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

On the main menu and on the game-over screen:

| Key | Action |
|---|---|
| `1` | start / retry |
| `2` | sound on/off (menu), main menu (game over) |
| `3` | capital letters on/off (menu) |
| Esc, Ctrl-C | quit |

Enter is an alias for `1` everywhere. Esc and Ctrl-C quit from any screen.

During play:

| Key | Action |
|---|---|
| `a`–`z`, `A`–`Z` | fire at the lowest matching letter |
| Tab | pause |
| Enter | clear the prompt |
| Backspace | delete from the prompt |
| Esc, Ctrl-C | quit |

Typing is case-sensitive: `a` does not hit `A`. Each shot leaves a mark on
the prompt — `•` hit, `◆` kill, `×` miss — and builds a streak multiplier
that applies at 10 in a row.

Both settings live in the menu and survive a retry and a trip back to the menu.
Turning capital letters off makes the rain entirely lowercase.

## Layout

```
src/
  util.*      rng, math, clock, utf-8          no dependencies
  palette.*   colour language                  no dependencies
  audio.*     procedural sfx + mixer           no dependencies (miniaudio, device only)
  sound.*     binds game events to effects     game, audio
  term.*      output buffer, raw mode, signals  POSIX
  canvas.*    cell grid + diff renderer        term, util
  game.*      simulation and rules             util, palette
  draw.*      paints game state to the canvas  canvas, game, palette
  input.*     stdin to key events              term, game
  main.c      wiring and the fixed-step loop
vendor/
  miniaudio.h  pinned single-header audio backend, unmodified
```

Dependencies point one way only. `game` includes neither `canvas.h` nor
`draw.h`, so the simulation never draws and can run without a terminal. That
boundary is what the test harness and the layering check both lean on.

Rendering is a diff against the previous frame, so a frame that changes
nothing emits no bytes.

## Sound

Five effects — hit, miss, kill, level up, game over — are synthesised from
oscillator sweeps and filtered noise at startup, so there are no sample files
and the game stays a single binary. Tweak the recipes at the top of
`src/audio.c`.

`audio` knows nothing about the game. `sound` is the one place that bridges
them: it reads the simulation's counters and turns the per-frame deltas into
effects. That keeps the dependency one-way and means the rules never learn
sound exists. Requests cross to the audio thread through a lock-free ring, so
the game never blocks on a full queue — sounds are dropped rather than queued.

Whether sound is wanted is a menu setting held by the game, not by `sound`, so
`game` can toggle it without depending on the audio stack. `sound_sync()` mirrors
that setting onto the mixer each frame via `audio_set_muted()`, which means
muting also cuts any effect still playing rather than waiting for it to finish.
Re-enabling is a silent baseline, so a muted stretch does not dump its worth of
events all at once.

If no output device is available (SSH, container, no sound card) `sound_open()`
reports it, the game prints a note, the menu shows sound as off, and everything
runs silently.

## Tests

```
make test
```

Five checks:

**Deterministic regression.** The binary can simulate itself with no tty and no
wall clock:

```
NEOTYPE_HEADLESS=1 NEOTYPE_SEED=1 NEOTYPE_W=100 NEOTYPE_H=30 ./neotype
```

It feeds a fixed key script into the sim and prints a digest every 60 frames —
score, level, streak, live drops, RNG state, and a hash of the rendered grid.
`tests/run.sh` runs six stimuli and diffs against `tests/golden.txt`. The set
covers all four states, levels 1 through 7, both the smallest and largest
supported viewports, and both ways out of a game over: retry, and back to the
menu with capital letters switched off. The longer stimulus also digests the
handful of frames where a single keypress changes the screen and the next one
changes it again, so screens that are up for one frame are still on record.

A digest mismatch means the sim, the RNG draw order, or the renderer changed.
If that was deliberate, review the diff and re-record with `make test UPDATE=1`.

**Layering check.** `tests/check-layers.sh` fails if a module includes one from
a higher layer, which keeps the dependency direction from eroding.

**Audio checks.** `tests/audio_test.c` drives the same device-free mixer the
game links against, so it needs no sound card and runs over SSH or in a
container. It asserts that every effect is audible and leaves headroom, that the
mixer is silent when idle, that a 500-request flood (ring overflow plus voice
stealing) stays clean and within full scale, and that muting suppresses
everything. To also write one `.wav` per effect so you can listen to them:

```
make test-audio OUT=/tmp/snd
```

**Sound-mapping check.** `tests/sound_test.c` drives the real simulation into
the real mapping and records which effect fired, by supplying `audio_play()`
itself. Nothing links `audio.o`, so it needs neither a sound card nor
miniaudio. It pins the counts — one kill sound per kill, one hit sound per
non-kill hit, one miss sound per miss, one game over per crash, one level up
per level change — plus the quiet cases that are easy to regress: nothing
before the run starts, nothing while retrying, the per-frame cap at 4,
`audio_init` reporting no device leaving the mapping inert, and muting from the
menu both silencing the effects and telling the mixer, so an effect already in
flight is cut too.

The first of those exists because it caught a real bug: the baseline was
zero-initialised while the game starts at level 1, so every run played the
level-up arpeggio on the main menu.

**Menu and settings check.** `tests/game_test.c` links `game.o` alone, so the
menu needs no canvas, terminal or audio. It pins the key contract (what `1`,
`2` and `3` do on each screen, that `q` no longer quits anywhere but Ctrl-C
still does, and that a crash swallows keys until its overlay settles), that both
settings survive a retry and a trip back to the menu, and that switching
capital letters off really does keep the rain lowercase.

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
