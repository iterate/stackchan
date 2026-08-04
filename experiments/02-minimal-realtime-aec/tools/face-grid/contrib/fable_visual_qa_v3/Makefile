# fvqa: visual acceptance tooling for the Stack-chan face renderers.
# Everything builds host-side; production sources are referenced
# read-only from firmware-ws/main.

EXPERIMENT := ../../../..
MAIN := $(EXPERIMENT)/firmware-ws/main
OUT := $(EXPERIMENT)/local/fable_visual_qa_v3/acceptance-v1
BUILD := build

CC ?= cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -DSTACKCHAN_HOST_TEST

RENDER_SOURCES := \
	$(MAIN)/face_abstract_redux.c \
	$(MAIN)/face_closeup_toon_actors.c \
	$(MAIN)/face_cyber_wildcards.c \
	$(MAIN)/face_eye_actors.c \
	$(MAIN)/face_eye_study_redux.c \
	$(MAIN)/face_mouth_actors.c \
	$(MAIN)/face_mouth_study_redux.c \
	$(MAIN)/face_pixel_pack.c \
	$(MAIN)/face_pixel_pack_core.c \
	$(MAIN)/face_pixel_pack_ega.c \
	$(MAIN)/face_pixel_pack_rogue.c \
	$(MAIN)/face_pixel_pack_talkie.c \
	$(MAIN)/face_pixel_pack_vga.c \
	$(MAIN)/face_pixel_redux_actors.c \
	$(MAIN)/face_render.c \
	$(MAIN)/face_robot_eyes.c \
	$(MAIN)/face_robot_eyes_behavior.c \
	$(MAIN)/face_robot_eyes_draw.c \
	$(MAIN)/face_robot_eyes_profiles.c \
	$(MAIN)/face_robot_redux_actors.c \
	$(MAIN)/face_salvage_actors.c \
	$(MAIN)/face_sprite_sheet.c \
	$(MAIN)/face_sprite_actors.c \
	$(MAIN)/face_sprite_redux_actors.c \
	$(MAIN)/face_sprite_showcase.c \
	$(MAIN)/face_stage.c \
	$(MAIN)/fta_act.c \
	$(MAIN)/fta_draw.c \
	$(MAIN)/fta_math.c \
	$(MAIN)/fta_styles.c

.PHONY: test probe run clean

test: $(BUILD)/fvqa_test
	$(BUILD)/fvqa_test

$(BUILD)/fvqa_test: fvqa_metrics.c fvqa_test.c fvqa_metrics.h | $(BUILD)
	$(CC) $(CFLAGS) fvqa_metrics.c fvqa_test.c -o $@

probe: $(BUILD)/fvqa_probe

$(BUILD)/fvqa_probe: fvqa_metrics.c fvqa_probe.c fvqa_metrics.h | $(BUILD)
	$(CC) $(CFLAGS) -I $(MAIN) fvqa_metrics.c fvqa_probe.c \
		$(RENDER_SOURCES) -o $@

run: $(BUILD)/fvqa_probe
	mkdir -p $(OUT)/sheets-ppm
	$(BUILD)/fvqa_probe $(OUT)
	python3 make_pngs.py $(OUT)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
