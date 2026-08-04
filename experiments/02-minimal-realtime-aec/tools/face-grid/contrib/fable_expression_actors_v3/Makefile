# fable_expression_actors_v3 — build/test/preview harness.
#
# Targets:
#   make test         strict native unit + acting-quality suite
#   make asan         same suite under ASan/UBSan (no recover)
#   make strict       O0 vs O2 frame-hash byte identity (determinism)
#   make bench        -O3 timing (us/frame per profile, host fps)
#   make sheets       labeled PPM/PNG contact sheets into preview/
#   make wasm-verify  emcc build; native vs wasm hash tables must match
#   make xtensa-check ESP32-S3 cross compile + float-symbol scan
#   make regen-golden refresh tests/golden_hashes.inc
#
# ABI headers come from compat/ (verbatim firmware copies). Point
# ABI_INC at ../../../../firmware-ws/main to build against the live
# tree instead; STAGE_SRC selects the face_stage.c the tests compile.

CC ?= cc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wshadow -Wconversion \
	-Wno-sign-conversion -Wdouble-promotion
ABI_INC ?= compat
STAGE_SRC ?= $(ABI_INC)/face_stage.c
INC = -Isrc -I$(ABI_INC)
SRC = src/fea_math.c src/fea_solve.c src/fea_draw.c \
	src/fea_actor_mochi.c src/fea_actor_karakuri.c \
	src/fea_actor_sticker.c src/fea_actor_wisp.c \
	src/fea_actor_scope.c src/fea_registry.c
BUILD = build

EMCC ?= emcc
XTENSA_CC ?= $(HOME)/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc

.PHONY: all test asan strict bench sheets wasm-verify xtensa-check \
	regen-golden clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_fea: tests/test_fea.c tests/golden_hashes.inc $(SRC) \
		$(STAGE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -Itests tests/test_fea.c $(SRC) \
		$(STAGE_SRC) -o $@

test: $(BUILD)/test_fea
	./$(BUILD)/test_fea

asan: | $(BUILD)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		$(INC) -Itests tests/test_fea.c $(SRC) $(STAGE_SRC) \
		-o $(BUILD)/test_fea_asan
	./$(BUILD)/test_fea_asan

$(BUILD)/fea_dump: tools/fea_dump.c $(SRC) $(STAGE_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $^ -o $@

strict: | $(BUILD)
	mkdir -p $(BUILD)/o0 $(BUILD)/o2
	$(CC) -std=c11 -O0 -g -Wall -Wextra -Werror $(INC) \
		tools/fea_dump.c $(SRC) $(STAGE_SRC) -o $(BUILD)/o0/fea_dump
	$(CC) -std=c11 -O2 -g -Wall -Wextra -Werror $(INC) \
		tools/fea_dump.c $(SRC) $(STAGE_SRC) -o $(BUILD)/o2/fea_dump
	./$(BUILD)/o0/fea_dump hash > $(BUILD)/o0/hashes.txt
	./$(BUILD)/o2/fea_dump hash > $(BUILD)/o2/hashes.txt
	cmp $(BUILD)/o0/hashes.txt $(BUILD)/o2/hashes.txt
	@echo "strict: O0 and O2 frame hashes are byte-identical"

bench: | $(BUILD)
	$(CC) -std=c11 -O3 -Wall -Wextra -Werror $(INC) \
		bench/bench_fea.c $(SRC) $(STAGE_SRC) -o $(BUILD)/bench_fea
	./$(BUILD)/bench_fea

sheets: $(BUILD)/fea_dump
	mkdir -p preview
	./$(BUILD)/fea_dump sheets preview
	@if command -v sips >/dev/null 2>&1; then \
		for f in preview/*.ppm; do \
			sips -s format png "$$f" --out "$${f%.ppm}.png" \
				>/dev/null || exit 1; \
		done; \
		echo "sheets: PPM + PNG written to preview/"; \
	else \
		echo "sheets: PPM written to preview/ (no sips; PNG skipped)"; \
	fi

# classic .js output auto-runs main; renamed to .cjs because the parent
# face-grid package.json declares `type: module`
wasm-verify: $(BUILD)/test_fea | $(BUILD)
	./$(BUILD)/test_fea --dump-hashes > $(BUILD)/hashes_host.txt
	$(EMCC) -std=c11 -O2 -Wall -Wextra -Werror $(INC) -Itests \
		-sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 \
		tests/test_fea.c $(SRC) $(STAGE_SRC) \
		-o $(BUILD)/test_fea_wasm.js
	mv $(BUILD)/test_fea_wasm.js $(BUILD)/test_fea_wasm.cjs
	node $(BUILD)/test_fea_wasm.cjs --dump-hashes \
		> $(BUILD)/hashes_wasm.txt
	diff $(BUILD)/hashes_host.txt $(BUILD)/hashes_wasm.txt
	node $(BUILD)/test_fea_wasm.cjs
	@echo "wasm-verify: native and wasm frame hashes are identical"

xtensa-check: | $(BUILD)
	mkdir -p $(BUILD)/xtensa
	for f in $(SRC); do \
		$(XTENSA_CC) -std=c11 -O2 -mlongcalls -Wall -Wextra -Werror \
			$(INC) -c $$f \
			-o $(BUILD)/xtensa/$$(basename $$f .c).o || exit 1; \
	done
	@! nm $(BUILD)/xtensa/*.o | \
		grep -E '__(add|sub|mul|div|float|fix|trunc|extend).*[sdt]f' \
		|| (echo "FLOAT CODE DETECTED" && exit 1)
	@echo "xtensa-check: cross compile clean, no float helpers linked"

regen-golden: $(BUILD)/test_fea
	./$(BUILD)/test_fea --write-golden > tests/golden_hashes.inc
	@echo "regen-golden: tests/golden_hashes.inc refreshed"

clean:
	rm -rf $(BUILD)
