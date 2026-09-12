CC = gcc
LUA ?= lua5.1
LUA_CFLAGS = $(shell pkg-config --cflags $(LUA) 2>/dev/null || pkg-config --cflags lua 2>/dev/null)
LUA_LIBS = $(shell pkg-config --libs $(LUA) 2>/dev/null || pkg-config --libs lua 2>/dev/null || echo '-l$(LUA)')

VERSION := $(shell git describe --tags --match 'v*' --always --dirty 2>/dev/null)
ifeq ($(VERSION),)
	VERSION := unknown
endif

CFLAGS = -std=c99 -Wall -Wextra -ggdb -D_DEFAULT_SOURCE -DVERSION='"$(VERSION)"'
INCLUDES = -Ithirdparty/conf.h -Ithirdparty/flag.h -Ithirdparty/cum.h $(LUA_CFLAGS)
LDLIBS = $(LUA_LIBS) -lm

SRC = src/main.c
BIN = todo
DEPS = thirdparty/conf.h thirdparty/flag.h thirdparty/cum.h

all: $(BIN)

$(BIN): $(SRC) deps
	$(CC) $(SRC) $(CFLAGS) $(INCLUDES) -o $(BIN) $(LDLIBS)

deps:
	@if [ -d .git ]; then \
		if git submodule status 2>/dev/null | grep -q '^-'; then \
			echo "Fetching submodules..."; \
			git submodule update --init --recursive; \
		fi; \
	else \
		for dep in $(DEPS); do \
			if [ ! -f "$$dep/$$(basename $$dep)" ]; then \
				case "$$dep" in \
					thirdparty/conf.h) url=https://github.com/hugoocoto/conf ;; \
					thirdparty/flag.h) url=https://github.com/hugoocoto/flag.h ;; \
					thirdparty/cum.h) url=https://github.com/hugoocoto/cum.h ;; \
				esac; \
				echo "No .git found; cloning $$dep from $$url..."; \
				rm -rf "$$dep"; \
				git clone --depth 1 "$$url" "$$dep"; \
			fi; \
		done; \
	fi

clean:
	rm -f $(BIN)

.PHONY: all clean deps
