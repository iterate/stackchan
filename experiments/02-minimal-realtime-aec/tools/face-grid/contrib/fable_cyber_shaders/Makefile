# fable_cyber_shaders — host tests, benchmarks, WASM byte-identity,
# and an ESP32-S3 cross-compilation check. Everything builds into
# ./build and writes previews into ./preview.

CC ?= cc
EMCC ?= emcc
XTENSA_CC ?= xtensa-esp32s3-elf-gcc

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -Isrc
SRC = src/cyber_face.c src/cyber_tables.c src/cyber_motion.c \
      src/cyber_profiles.c
HDR = src/cyber_face.h src/cyber_internal.h

.PHONY: all test bench preview regen-golden wasm-verify xtensa-check clean

all: test bench

build:
	mkdir -p build

preview-dir:
	mkdir -p preview

build/test_cyber_face: $(SRC) $(HDR) tests/test_cyber_face.c \
		tests/golden_hashes.inc | build
	$(CC) $(CFLAGS) -Itests $(SRC) tests/test_cyber_face.c -o $@

build/bench_cyber_face: $(SRC) $(HDR) bench/bench_cyber_face.c | build
	$(CC) $(CFLAGS) $(SRC) bench/bench_cyber_face.c -o $@

build/dump_previews: $(SRC) $(HDR) tools/dump_previews.c | build
	$(CC) $(CFLAGS) $(SRC) tools/dump_previews.c -o $@

test: build/test_cyber_face
	./build/test_cyber_face

bench: build/bench_cyber_face
	./build/bench_cyber_face

preview: build/dump_previews preview-dir
	./build/dump_previews preview

regen-golden: build/test_cyber_face
	./build/test_cyber_face --dump-hashes > tests/golden_hashes.inc
	@echo "golden hashes regenerated; re-run make test"

# Byte-identity: the WASM build must print the identical hash table.
build/test_wasm.js: $(SRC) $(HDR) tests/test_cyber_face.c \
		tests/golden_hashes.inc | build
	$(EMCC) -std=c11 -O2 -Wall -Wextra -Werror -Isrc -Itests \
		-sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 \
		$(SRC) tests/test_cyber_face.c -o $@

wasm-verify: build/test_cyber_face build/test_wasm.js
	./build/test_cyber_face --dump-hashes > build/hashes_host.txt
	node build/test_wasm.js --dump-hashes > build/hashes_wasm.txt
	diff build/hashes_host.txt build/hashes_wasm.txt
	node build/test_wasm.js
	@echo "WASM output is byte-identical to host output"

# Cross-compile for ESP32-S3, prove the hot path stays float-free, and
# report code size. Requires the ESP-IDF xtensa toolchain on PATH or an
# explicit XTENSA_CC.
xtensa-check: | build
	mkdir -p build/xtensa
	for f in $(SRC); do \
		$(XTENSA_CC) -std=c11 -O2 -mlongcalls -Wall -Wextra -Werror \
			-Isrc -c $$f -o build/xtensa/$$(basename $$f .c).o \
			|| exit 1; \
	done
	@echo "--- float/soft-float symbol scan (expect no output) ---"
	@! nm build/xtensa/*.o | grep -E '__(add|sub|mul|div|float|fix|trunc|extend).*[sdt]f' \
		|| (echo "FLOAT CODE DETECTED" && exit 1)
	@echo "--- section sizes ---"
	@$(XTENSA_CC:gcc=size) build/xtensa/*.o 2>/dev/null || \
		size build/xtensa/*.o
	@echo "xtensa-check passed: no floating-point helpers referenced"

clean:
	rm -rf build preview
