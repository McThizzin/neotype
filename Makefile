# neotype - matrix-rain typing shooter for the terminal
#
#   make            build ./neotype
#   make test       deterministic headless regression + layering + audio checks
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

.PHONY: all test test-audio clean
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c -o $@ $<

# Device-free: links the same mixer the game uses, but never opens a sound
# card, so it runs anywhere. OPTIONAL first arg = directory for .wav exports.
$(ATEST): tests/audio_test.c src/audio.o src/util.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(BIN) $(ATEST)
	@sh tests/run.sh
	@sh tests/check-layers.sh
	@$(ATEST)

test-audio: $(ATEST)
	@if [ -n "$(OUT)" ]; then mkdir -p "$(OUT)"; fi
	@$(ATEST) $(OUT)

clean:
	rm -f $(BIN) $(OBJ) $(DEP) $(ATEST)

-include $(DEP)
