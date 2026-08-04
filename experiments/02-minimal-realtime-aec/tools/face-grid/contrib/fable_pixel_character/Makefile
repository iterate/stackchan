# fable_pixel_character — host build, tests, review images, WASM parity.
#
# The renderer sources are standalone portable C11. `compat/` holds verbatim
# copies of the firmware ABI headers (face_keyframe.h, face_pose.h); point
# ABI_INC at firmware-ws/main to compile against the live ones instead.

CC ?= cc
EMCC ?= emcc
NODE ?= node
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic
ABI_INC ?= compat
INC = -Isrc -I$(ABI_INC)

BUILD = build
OUT = out

RENDER_SRC = \
	src/pf_engine.c \
	src/pf_idle.c \
	src/pf_mouth.c \
	src/pf_profiles.c \
	src/pf_r_ega_quest.c \
	src/pf_r_vga_elder.c \
	src/pf_r_talkie.c \
	src/pf_r_automaton.c \
	src/pf_r_terminal.c \
	src/pf_r_pocket_rpg.c \
	src/pf_r_onebit.c \
	src/pf_r_attribute.c \
	src/pf_r_cga.c \
	src/pf_r_c64.c

.PHONY: all test frames golden verify-wasm bench clean

all: $(BUILD)/test_pixel_face $(BUILD)/dump_frames $(BUILD)/bench

$(BUILD):
	mkdir -p $(BUILD) $(OUT)

$(BUILD)/test_pixel_face: tests/test_pixel_face.c $(RENDER_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -Itests $^ -o $@

$(BUILD)/dump_frames: tools/dump_frames.c tools/png_write.c $(RENDER_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -Itools $^ -o $@

$(BUILD)/bench: tools/bench.c $(RENDER_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $^ -o $@

test: $(BUILD)/test_pixel_face
	./$(BUILD)/test_pixel_face

golden: $(BUILD)/test_pixel_face
	./$(BUILD)/test_pixel_face --write-golden tests/golden_crcs.inc

frames: $(BUILD)/dump_frames
	./$(BUILD)/dump_frames $(OUT)

bench: $(BUILD)/bench
	./$(BUILD)/bench

# Byte-identical WASM parity: build the test binary with emcc, run its CRC
# dump under node, and diff against the native CRC dump.
verify-wasm: $(BUILD)/test_pixel_face
	$(EMCC) -std=c11 -O2 -Wall -Wextra -Werror $(INC) -Itests \
		tests/test_pixel_face.c $(RENDER_SRC) \
		-sENVIRONMENT=node -o $(BUILD)/test_pixel_face_wasm.js
	./$(BUILD)/test_pixel_face --print-crcs > $(BUILD)/crcs_native.txt
	$(NODE) $(BUILD)/test_pixel_face_wasm.js --print-crcs > $(BUILD)/crcs_wasm.txt
	diff $(BUILD)/crcs_native.txt $(BUILD)/crcs_wasm.txt
	@echo "WASM output is byte-identical to native output"

clean:
	rm -rf $(BUILD)
