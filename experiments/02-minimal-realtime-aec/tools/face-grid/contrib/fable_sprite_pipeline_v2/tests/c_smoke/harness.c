/* FSPP C smoke test.
 *
 * Compiles a generated FSPP atlas together with the production
 * firmware-ws/main/face_sprite_sheet.c player and proves that:
 *
 *   1. face_sprite_player_init() accepts the atlas (full structural
 *      validation: magic, palette ranges, PackBits decode, placements);
 *   2. a sweep across expression targets, visemes, blinks, and gaze
 *      renders without error into a caller-owned RGB565 frame;
 *   3. replaying the identical keys and clocks reproduces bit-identical
 *      pixels (FNV-1a over every frame matches across two passes).
 *
 * Build (see Makefile target c-smoke):
 *   cc -std=c11 -O2 -Wall -Wextra -Werror \
 *      -I$STACKCHAN_MAIN_DIR -DFSPP_ATLAS_SYMBOL=<symbol> \
 *      harness.c <atlas>.c $STACKCHAN_MAIN_DIR/face_sprite_sheet.c
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_sprite_sheet.h"

#ifndef FSPP_ATLAS_SYMBOL
#error "define FSPP_ATLAS_SYMBOL to the generated atlas symbol"
#endif

extern const face_sprite_atlas_t FSPP_ATLAS_SYMBOL;

static uint16_t frame[FACE_RENDER_PIXEL_COUNT];

typedef struct {
    int8_t valence;
    uint8_t arousal;
    int8_t corner;
    int8_t brow_inner;
    uint8_t squint;
} sweep_target_t;

/* The eleven canonical action targets used by the generated banks. */
static const sweep_target_t TARGETS[] = {
    {0, 72, 0, 0, 0},      {52, 112, 36, 10, 20},  {94, 184, 78, 22, 68},
    {-52, 126, -24, 58, 14}, {18, 232, 8, 78, 0},  {4, 92, -6, 18, 22},
    {-18, 104, -12, -14, 42}, {12, 166, -8, -34, 54}, {8, 34, 4, -26, 118},
    {86, 250, 62, 52, 14}, {28, 176, 24, 24, 116},
};

static uint32_t fnv1a(uint32_t hash, const uint8_t *data, size_t bytes)
{
    for (size_t i = 0; i < bytes; i++) {
        hash = (hash ^ data[i]) * 0x01000193u;
    }
    return hash;
}

static void build_key(face_render_key_t *key, size_t target, size_t step)
{
    memset(key, 0, sizeof(*key));
    key->controls.eye_left_open = 255;
    key->controls.eye_right_open = 255;
    key->controls.mouth_open = (uint8_t)(step * 37u % 256u);
    key->controls.mouth_width = (uint8_t)(step * 53u % 256u);
    key->controls.look_x = (int8_t)((int)(step % 5u) * 20 - 40);
    key->controls.look_y = (int8_t)((int)(step % 3u) * 20 - 20);
    key->controls.flags = (step % 7u == 3u)
        ? FACE_KEYFRAME_FLAG_BLINKING
        : 0u;
    key->schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key->expression_weight = 255;
    key->affect_valence = TARGETS[target].valence;
    key->affect_arousal = TARGETS[target].arousal;
    key->mouth_corner_left = TARGETS[target].corner;
    key->mouth_corner_right = TARGETS[target].corner;
    key->brow_inner = TARGETS[target].brow_inner;
    key->eye_left_squint = TARGETS[target].squint;
    key->eye_right_squint = TARGETS[target].squint;
    if (step % 2u == 0u) {
        key->viseme_set = (uint8_t)(step % 4u); /* OVR15/VRM5/P9/MS22 */
        key->viseme = (uint8_t)(step % 5u);
        key->viseme_weight = 255;
        key->speech_phase = 2; /* FACE_SPEECH_ACTIVE */
        key->controls.flags |= FACE_KEYFRAME_FLAG_SPEAKING;
    }
}

static int render_pass(uint32_t *hash_out)
{
    face_sprite_player_t player;
    if (!face_sprite_player_init(&player, &FSPP_ATLAS_SYMBOL)) {
        fprintf(stderr, "FAIL: face_sprite_player_init rejected atlas\n");
        return 1;
    }
    uint32_t hash = 0x811c9dc5u;
    uint32_t clock = 0;
    size_t nonzero_frames = 0;
    for (size_t target = 0; target < 11; target++) {
        for (size_t step = 0; step < 8; step++) {
            face_render_key_t key;
            build_key(&key, target, step);
            clock += 5333; /* one 30 fps frame of 16 kHz samples */
            if (!face_sprite_render(
                    &player, &key, clock, frame,
                    FACE_RENDER_PIXEL_COUNT)) {
                fprintf(
                    stderr,
                    "FAIL: render target %zu step %zu\n",
                    target,
                    step);
                return 1;
            }
            size_t filled = 0;
            for (size_t i = 0; i < FACE_RENDER_PIXEL_COUNT; i++) {
                if (frame[i] != FSPP_ATLAS_SYMBOL.background) {
                    filled++;
                }
            }
            if (filled > 0) {
                nonzero_frames++;
            }
            hash = fnv1a(
                hash, (const uint8_t *)frame, sizeof(frame));
        }
    }
    if (nonzero_frames == 0) {
        fprintf(stderr, "FAIL: every frame was background only\n");
        return 1;
    }
    *hash_out = hash;
    return 0;
}

int main(void)
{
    uint32_t first = 0;
    uint32_t second = 0;
    if (render_pass(&first) || render_pass(&second)) {
        return 1;
    }
    if (first != second) {
        fprintf(
            stderr,
            "FAIL: replay hash mismatch %08" PRIx32 " vs %08" PRIx32 "\n",
            first,
            second);
        return 1;
    }
    printf(
        "PASS %s frames=88 fnv1a=%08" PRIx32 "\n",
        FSPP_ATLAS_SYMBOL.name,
        first);
    return 0;
}
