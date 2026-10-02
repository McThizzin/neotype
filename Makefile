# neotype - matrix-rain typing shooter for the terminal
#
#   make          build ./neotype
#   make test     deterministic headless regression + layering check
#   make clean

CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
LDLIBS  := -lm
DEPFLAGS := -MMD -MP

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)

BIN := neotype

.PHONY: all test clean
all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc $(DEPFLAGS) -c -o $@ $<

test: $(BIN)
	@sh tests/run.sh
	@sh tests/check-layers.sh

clean:
	rm -f $(BIN) $(OBJ) $(DEP)

-include $(DEP)
