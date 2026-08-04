# Standalone build for the wildcard renderer prototypes.
#
# Targets:
#   make test        - build and run the native C test suite
#   make bench       - build and run the throughput benchmark
#   make frames      - build the frame dumper used by tools/make_previews.sh
#   make wasm-check  - build the test suite with emscripten and require that
#                      every golden CRC matches the native build byte for byte
#   make clean

CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wshadow
BUILD := build

SRC := \
	src/wc_common.c \
	src/wc_scope_beam.c \
	src/wc_flipdot_cascade.c \
	src/wc_chladni_sand.c \
	src/wc_halftone_press.c \
	src/wc_wayang_lamp.c \
	src/wc_ferro_pool.c \
	src/wc_teletext_sextant.c \
	src/wildcard_face.c

.PHONY: test bench frames wasm-check clean

$(BUILD):
	mkdir -p $(BUILD)

test: | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(SRC) test/test_wildcards.c -o $(BUILD)/test_wildcards
	./$(BUILD)/test_wildcards

bench: | $(BUILD)
	$(CC) $(CFLAGS) -DWC_BENCH=1 -Isrc $(SRC) test/test_wildcards.c -o $(BUILD)/bench_wildcards
	./$(BUILD)/bench_wildcards

frames: | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(SRC) tools/render_frames.c -o $(BUILD)/render_frames

wasm-check: | $(BUILD)
	emcc -O2 -std=c11 -Isrc $(SRC) test/test_wildcards.c \
		-sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 \
		-o $(BUILD)/test_wildcards.mjs
	node $(BUILD)/test_wildcards.mjs

clean:
	rm -rf $(BUILD)
