# FSPP: fable sprite pipeline v2.
#
# Everything is stdlib python3 + cc. STACKCHAN_MAIN_DIR must point at
# firmware-ws/main (defaults to the in-repo relative path).

CC ?= cc
PYTHON ?= python3
STACKCHAN_MAIN_DIR ?= ../../../../firmware-ws/main
MOSSLING_SHEET ?= ../../assets/mossling/pocket-mossling-expression-viseme-concepts-v1.png
LOCAL_DIR ?= ../../../../local/fable_sprite_pipeline_v2

BUILD := build
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Wshadow -pedantic

BLOOMLING_ATLAS := samples/bloomling/cores3-face/fspp_bloomling_cores3_face_atlas.c
BLOOMLING_SYMBOL := face_sprite_fspp_bloomling_cores3_face_atlas
MOSSLING_ATLAS := samples/mossling/cores3-face/fspp_mossling_cores3_face_atlas.c
MOSSLING_SYMBOL := face_sprite_fspp_mossling_cores3_face_atlas

.PHONY: all fixture test samples sample-bloomling sample-mossling \
	c-smoke c-smoke-mossling brief clean

all: fixture test samples c-smoke

fixture:
	$(PYTHON) -B fixtures/make_fixture_sheet.py

test:
	$(PYTHON) -B tests/run_tests.py

sample-bloomling: fixture
	$(PYTHON) -B spritepipe.py build \
		--spec fixtures/bloomling_spec.json \
		--out samples/bloomling \
		--debug-dir $(LOCAL_DIR)/bloomling-debug

sample-mossling:
	$(PYTHON) -B spritepipe.py build \
		--spec fixtures/mossling_spec.json \
		--sheet $(MOSSLING_SHEET) \
		--out samples/mossling \
		--debug-dir $(LOCAL_DIR)/mossling-debug

samples: sample-bloomling sample-mossling

brief:
	$(PYTHON) -B spritepipe.py brief \
		--spec fixtures/sproutling_prompt_spec.json \
		--out $(LOCAL_DIR)/sproutling-brief

$(BUILD):
	mkdir -p $(BUILD)

c-smoke: $(BUILD)
	$(CC) $(CFLAGS) -I$(STACKCHAN_MAIN_DIR) \
		-Isamples/bloomling/cores3-face \
		-DFSPP_ATLAS_SYMBOL=$(BLOOMLING_SYMBOL) \
		tests/c_smoke/harness.c \
		$(BLOOMLING_ATLAS) \
		$(STACKCHAN_MAIN_DIR)/face_sprite_sheet.c \
		-o $(BUILD)/c_smoke_bloomling
	./$(BUILD)/c_smoke_bloomling

c-smoke-mossling: $(BUILD)
	$(CC) $(CFLAGS) -I$(STACKCHAN_MAIN_DIR) \
		-Isamples/mossling/cores3-face \
		-DFSPP_ATLAS_SYMBOL=$(MOSSLING_SYMBOL) \
		tests/c_smoke/harness.c \
		$(MOSSLING_ATLAS) \
		$(STACKCHAN_MAIN_DIR)/face_sprite_sheet.c \
		-o $(BUILD)/c_smoke_mossling
	./$(BUILD)/c_smoke_mossling

clean:
	rm -rf $(BUILD) __pycache__ fspp/__pycache__
