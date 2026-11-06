CC ?= cc
CFLAGS ?= -O3 -g -std=c11 -Wall -Wextra -Wpedantic -Werror
BUILD := work/build
CORE := src/tr66.c src/patterns.c src/sequencer.c src/controls.c
HEADERS := src/tr66.h src/patterns.h src/sequencer.h src/controls.h src/voice_patches.inc

.PHONY: all test
all: $(BUILD)/libtr66.so

$(BUILD):
	mkdir -p $@

$(BUILD)/libtr66.so: $(CORE) $(HEADERS) tools/render_stem.c | $(BUILD)
	$(CC) $(CFLAGS) -fPIC -shared -Isrc $(CORE) tools/render_stem.c -o $@ -lm

$(BUILD)/test-tr66: $(CORE) $(HEADERS) tests/test_tr66.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE) tests/test_tr66.c -o $@ -lm

test: $(BUILD)/test-tr66
	$(BUILD)/test-tr66

$(BUILD)/tr66-render: $(CORE) $(HEADERS) tools/render.c | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE) tools/render.c -o $@ -lm

.PHONY: render
render: $(BUILD)/tr66-render
