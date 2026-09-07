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

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(SRC) $(CFLAGS) $(INCLUDES) -o $(BIN) $(LDLIBS)

clean:
	rm -f $(BIN)

.PHONY: all clean
