# quic-siege -- raw QUIC connection/stream siege tool built on liblsquic
#
# Override LSQUIC_PREFIX if you installed lsquic somewhere other than
# /usr/local (e.g. `make LSQUIC_PREFIX=$HOME/opt/lsquic`).

LSQUIC_PREFIX ?= /usr/local

CC       ?= cc
CFLAGS   ?= -O2 -g -Wall -Wextra -std=c11
CPPFLAGS := -D_GNU_SOURCE -I$(LSQUIC_PREFIX)/include $(CPPFLAGS)
LDFLAGS  := -L$(LSQUIC_PREFIX)/lib $(LDFLAGS)
LDLIBS   := -llsquic $(shell pkg-config --libs openssl 2>/dev/null || echo -lssl -lcrypto) -lz -lpthread -lm

SRC := src/stats.c src/worker.c src/main.c
OBJ := $(SRC:.c=.o)
BIN := quic-siege

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)
