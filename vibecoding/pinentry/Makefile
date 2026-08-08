CXX      := g++
CXXFLAGS := -std=c++23 -Wall -Wextra -O2
PKGCONFIG:= $(shell pkg-config --exists raylib && echo yes || echo no)

# --- pinentry-helper: spawns the system pinentry, no GUI deps ---
DEMO_TARGET := pinentry-helper
DEMO_SRCS   := main.cpp pinentry_client.cpp

# --- pinentry-raylib: a pinentry implementation drawn with raylib+raygui ---
RAYLIB_TARGET := pinentry-raylib
RAYLIB_SRCS    := pinentry_raylib.cpp

ifeq ($(PKGCONFIG),yes)
RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib)
RAYLIB_LIBS   := $(shell pkg-config --libs raylib) -lGL -lm -lpthread -ldl -lrt -lX11
else
# Fallback if raylib wasn't installed via pkg-config (e.g. built from
# source with `make install PREFIX=/usr/local` as raylib's own Makefile
# does). Adjust -I/-L if you installed elsewhere.
RAYLIB_CFLAGS := -I/usr/local/include
RAYLIB_LIBS   := -L/usr/local/lib -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
endif

all: $(DEMO_TARGET) $(RAYLIB_TARGET)

$(DEMO_TARGET): $(DEMO_SRCS) pinentry_client.hpp
	$(CXX) $(CXXFLAGS) $(DEMO_SRCS) -o $(DEMO_TARGET)

$(RAYLIB_TARGET): $(RAYLIB_SRCS) raygui.h
	$(CXX) $(CXXFLAGS) $(RAYLIB_CFLAGS) $(RAYLIB_SRCS) -o $(RAYLIB_TARGET) $(RAYLIB_LIBS)

clean:
	rm -f $(DEMO_TARGET) $(RAYLIB_TARGET)

.PHONY: all clean
