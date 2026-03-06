CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -O2
LDFLAGS =

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    # macOS (Homebrew)
    RAYLIB_PREFIX  = $(shell brew --prefix raylib 2>/dev/null || echo /opt/homebrew/opt/raylib)
    CURL_PREFIX    = $(shell brew --prefix curl 2>/dev/null || echo /opt/homebrew/opt/curl)
    ARCHIVE_PREFIX = $(shell brew --prefix libarchive 2>/dev/null || echo /opt/homebrew/opt/libarchive)
    CFLAGS  += -I$(RAYLIB_PREFIX)/include -I$(CURL_PREFIX)/include -I$(ARCHIVE_PREFIX)/include
    LDFLAGS += -L$(RAYLIB_PREFIX)/lib -lraylib
    LDFLAGS += -L$(CURL_PREFIX)/lib -lcurl
    LDFLAGS += -L$(ARCHIVE_PREFIX)/lib -larchive
    LDFLAGS += -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo
else ifeq ($(UNAME_S),Linux)
    # Linux (pkg-config)
    CFLAGS  += $(shell pkg-config --cflags raylib libcurl libarchive 2>/dev/null)
    LDFLAGS += $(shell pkg-config --libs raylib libcurl libarchive 2>/dev/null)
    LDFLAGS += -lm -lpthread -ldl
endif

SRC = src/main.c src/setup.c src/platform.c
OUT = ds-sd-setup

.PHONY: all clean run

all: $(OUT)

$(OUT): $(SRC) src/setup.h src/platform.h
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: $(OUT)
	./$(OUT)

clean:
	rm -f $(OUT)
