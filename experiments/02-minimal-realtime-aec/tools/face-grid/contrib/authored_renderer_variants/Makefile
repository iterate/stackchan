CC ?= cc
EMCC ?= emcc

ROOT := ../../../..
FTA := ../fable_toon_acting
BUILD := build
PREVIEW := preview

WARN := -Wall -Wextra -Werror -Wshadow -pedantic
CFLAGS := -std=c11 -O2 $(WARN) -Isrc -I$(ROOT)/firmware-ws/main -I$(FTA)/src

BASE_SOURCES := \
	src/face_authored_variants.c \
	$(ROOT)/firmware-ws/main/face_sprite_actors.c \
	$(FTA)/src/fta_math.c \
	$(FTA)/src/fta_act.c \
	$(FTA)/src/fta_draw.c \
	$(ROOT)/firmware-ws/main/fta_styles.c

FIXTURE_SOURCES := $(ROOT)/firmware-ws/main/face_stage.c

.PHONY: all test asan ubsan strict sheets wasm-verify clean

all: test

$(BUILD):
	mkdir -p $(BUILD)

$(PREVIEW):
	mkdir -p $(PREVIEW)

$(BUILD)/test_fav: tests/test_face_authored_variants.c $(BASE_SOURCES) $(FIXTURE_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/dump_fav: tools/dump_face_authored_variants.c $(BASE_SOURCES) $(FIXTURE_SOURCES) | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

test: $(BUILD)/test_fav
	./$(BUILD)/test_fav

asan: | $(BUILD)
	$(CC) -std=c11 -O1 -g $(WARN) \
		-fsanitize=address -fno-omit-frame-pointer \
		-Isrc -I$(ROOT)/firmware-ws/main -I$(FTA)/src \
		tests/test_face_authored_variants.c $(BASE_SOURCES) $(FIXTURE_SOURCES) \
		-o $(BUILD)/test_fav_asan
	./$(BUILD)/test_fav_asan

ubsan: | $(BUILD)
	$(CC) -std=c11 -O1 -g $(WARN) \
		-fsanitize=undefined -fno-sanitize-recover=all \
		-Isrc -I$(ROOT)/firmware-ws/main -I$(FTA)/src \
		tests/test_face_authored_variants.c $(BASE_SOURCES) $(FIXTURE_SOURCES) \
		-o $(BUILD)/test_fav_ubsan
	./$(BUILD)/test_fav_ubsan

strict: tools/dump_face_authored_variants.c $(BASE_SOURCES) $(FIXTURE_SOURCES) | $(BUILD)
	$(CC) -std=c11 -O0 -g $(WARN) -Isrc -I$(ROOT)/firmware-ws/main -I$(FTA)/src $^ -o $(BUILD)/dump_fav_o0
	$(CC) -std=c11 -O2 $(WARN) -Isrc -I$(ROOT)/firmware-ws/main -I$(FTA)/src $^ -o $(BUILD)/dump_fav_o2
	./$(BUILD)/dump_fav_o0 hash > $(BUILD)/hashes_o0.txt
	./$(BUILD)/dump_fav_o2 hash > $(BUILD)/hashes_o2.txt
	cmp $(BUILD)/hashes_o0.txt $(BUILD)/hashes_o2.txt

sheets: $(BUILD)/dump_fav | $(PREVIEW)
	./$(BUILD)/dump_fav sheets $(PREVIEW)

wasm-verify: tools/dump_face_authored_variants.c $(BASE_SOURCES) $(FIXTURE_SOURCES) | $(BUILD)
	./$(BUILD)/dump_fav hash > $(BUILD)/hashes_host.txt
	$(EMCC) -std=c11 -O2 $(WARN) \
		-Isrc -I$(ROOT)/firmware-ws/main -I$(FTA)/src \
		-sENVIRONMENT=node -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 \
		$^ -o $(BUILD)/dump_fav_wasm.js
	mv $(BUILD)/dump_fav_wasm.js $(BUILD)/dump_fav_wasm.cjs
	node $(BUILD)/dump_fav_wasm.cjs hash > $(BUILD)/hashes_wasm.txt
	cmp $(BUILD)/hashes_host.txt $(BUILD)/hashes_wasm.txt

clean:
	rm -rf $(BUILD) $(PREVIEW)
