CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -O2 \
           -Iinclude
LDFLAGS :=

TARGET  := git-credential-helper

SRCS    := src/main.c \
           src/credential_store.c \
           lib/argtable3.c

OBJS    := $(patsubst %.c, build/%.o, $(SRCS))

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built: $(TARGET)"

build/src/%.o: src/%.c | build/src
	$(CC) $(CFLAGS) -c $< -o $@

build/lib/%.o: lib/%.c | build/lib
	$(CC) $(CFLAGS) -c $< -o $@

build/src build/lib:
	mkdir -p $@

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"
	@echo "To enable:"
	@echo "  git config --global credential.helper $(TARGET)"

clean:
	rm -rf build $(TARGET)

# ── Integration tests ────────────────────────────────────────────────────
test: $(TARGET)
	@echo "=== Test: add credential ==="
	GIT_CREDENTIAL_STORE=/tmp/test-creds-$$$$.store \
	  ./$(TARGET) add --host test.example.com --username alice --password s3cr3t --protocol https && \
	  echo PASS || echo FAIL

	@echo "=== Test: list ==="
	GIT_CREDENTIAL_STORE=/tmp/test-creds.store \
	  ./$(TARGET) list || true

	@echo "=== Test: git get protocol ==="
	printf "protocol=https\nhost=test.example.com\n\n" | \
	GIT_CREDENTIAL_STORE=/tmp/test-creds.store \
	  ./$(TARGET) get || true

	@echo "=== Test: version flag ==="
	./$(TARGET) --version

	@echo "=== Tests done ==="
