# neotype - matrix-rain typing shooter for the terminal
#
#   make            build ./neotype
#   make test       headless regression + layering + audio + sound + game checks
#   make test-audio audio checks only (pass OUT=dir to also write .wav files)
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
LDLIBS  := -lm
DEPFLAGS := -MMD -MP

# miniaudio needs dlopen() and pthread_create(). glibc 2.34+ folds both into
# libc, older releases still want them named -- and macOS has neither as a
# separate library, so only ask for them on Linux.
ifeq ($(shell uname -s),Linux)
LDLIBS += -lpthread -ldl
endif

# -I. resolves the vendored single-header miniaudio in src/audio.c.
CPPFLAGS := -Isrc -I.

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)

BIN   := neotype
ATEST := tests/audio_test
STEST := tests/sound_test
GTEST := tests/game_test

.PHONY: all test test-audio test-sound test-game clean
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c -o $@ $<

# Device-free: links the same mixer the game uses, but never opens a sound
# card, so it runs anywhere. OPTIONAL first arg = directory for .wav exports.
$(ATEST): tests/audio_test.c src/audio.o src/util.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# No audio.o on the link line: the test supplies audio_play() itself to record
# what fired, so this needs neither a sound card nor miniaudio.
$(STEST): tests/sound_test.c src/sound.o src/game.o src/palette.o src/util.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Same set as the sound test minus audio.o: palette is pure colour math with no
# canvas dependency, so still no terminal and no audio.
$(GTEST): tests/game_test.c src/game.o src/palette.o src/util.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BIN) $(ATEST) $(STEST) $(GTEST)
	@sh tests/run.sh
	@sh tests/check-layers.sh
	@$(ATEST)
	@$(STEST)
	@$(GTEST)

test-audio: $(ATEST)
	@if [ -n "$(OUT)" ]; then mkdir -p "$(OUT)"; fi
	@$(ATEST) $(OUT)

test-sound: $(STEST)
	@$(STEST)

test-game: $(GTEST)
	@$(GTEST)

clean:
	rm -f $(BIN) $(OBJ) $(DEP) $(ATEST) $(STEST) $(GTEST)

-include $(DEP)
