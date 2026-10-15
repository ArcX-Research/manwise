CC = cc
PYTHON ?= python3
BUILD ?= build
PREFIX ?= /usr/local
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE -DCJSON_NESTING_LIMIT=32 -Isrc/include -Ivendor/cjson
SOURCES = src/c/main.c src/c/json.c src/c/text.c src/c/process.c src/c/manuals.c src/c/protocol.c src/c/server.c vendor/cjson/cJSON.c
HEADERS = $(wildcard src/include/*.h) vendor/cjson/cJSON.h

.PHONY: all test sanitize install clean boundscope test-sdk
all: $(BUILD)/manwise

$(BUILD):
	mkdir -p "$@"

$(BUILD)/manwise: $(SOURCES) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $(SOURCES) $(LDFLAGS) -o "$@"

$(BUILD)/test-core: tests/c/core.c src/c/json.c src/c/text.c vendor/cjson/cJSON.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) tests/c/core.c src/c/json.c src/c/text.c vendor/cjson/cJSON.c $(LDFLAGS) -o "$@"

$(BUILD)/fake-man: tests/c/fake_man.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) $< $(LDFLAGS) -o "$@"

$(BUILD)/test-server: $(SOURCES) $(HEADERS) $(BUILD)/fake-man | $(BUILD)
	$(CC) $(CPPFLAGS) -DMW_MAN='"$(abspath $(BUILD)/fake-man)"' $(CFLAGS) $(WARN) $(SOURCES) $(LDFLAGS) -o "$@"

test: $(BUILD)/manwise $(BUILD)/test-core $(BUILD)/test-server
	"$(BUILD)/test-core" all
	$(PYTHON) tests/python/test_server.py "$(BUILD)"

sanitize:
	$(MAKE) test CC=clang BUILD=build/sanitize CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'

boundscope:
	$(PYTHON) scripts/python/check_boundscope.py

test-sdk: $(BUILD)/manwise
	$(PYTHON) tests/python/test_sdk.py "$(BUILD)/manwise"

install: $(BUILD)/manwise
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 "$(BUILD)/manwise" "$(DESTDIR)$(PREFIX)/bin/manwise"

clean:
	rm -rf build
