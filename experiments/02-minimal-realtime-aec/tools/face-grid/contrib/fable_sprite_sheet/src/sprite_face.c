#include "sprite_face.h"

#include <string.h>

/*
 * Layer compositing order and the base-plus-patches model mirror the
 * Sierra SCI portrait player (base bitmap + timed mouth overlay) and
 * the AGS ten-frame talking-view convention; timing constants follow
 * Rhubarb Lip Sync. See docs/RESEARCH.md for sources.
 */

enum {
    SPRITE_MAX_NATIVE_WIDTH = SPRITE_FACE_WIDTH,
    SPRITE_MAX_PALETTE = 256,
    SPRITE_SACCADE_MOVE = 1067, /* two 30 fps frames, in samples */
};

typedef struct {
    uint16_t *pixels;
    const sprite_atlas_t *atlas;
    const uint16_t *palette;
    int32_t origin_x;
    int32_t origin_y;
} blit_ctx_t;

typedef struct {
    const uint8_t *data;
    uint32_t remaining;
    uint32_t run_count;
    uint32_t literal_count;
    uint8_t run_value;
    uint8_t encoding;
} cell_stream_t;

typedef struct {
    int16_t saccade_x;
    int16_t saccade_y;
    int16_t bob;
    uint8_t blink;
} sprite_motion_t;

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static uint8_t max_u8(uint8_t left, uint8_t right)
{
    return left > right ? left : right;
}

static uint8_t mul_u8(uint8_t left, uint8_t right)
{
    return (uint8_t)(((uint16_t)left * right + 127u) / 255u);
}

static uint32_t hash_u32(uint32_t value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

/* ------------------------------------------------------------------ */
/* Cell stream decoding                                               */

static void stream_open(
    cell_stream_t *stream,
    const sprite_atlas_t *atlas,
    const sprite_cell_t *cell)
{
    stream->data = atlas->blob + cell->data_offset;
    stream->remaining = cell->data_length;
    stream->run_count = 0;
    stream->literal_count = 0;
    stream->run_value = 0;
    stream->encoding = cell->encoding;
}

static bool stream_next_row(
    cell_stream_t *stream, uint8_t *row, uint32_t width)
{
    if (stream->encoding == SPRITE_CELL_ENCODING_RAW) {
        if (stream->remaining < width) {
            return false;
        }
        memcpy(row, stream->data, width);
        stream->data += width;
        stream->remaining -= width;
        return true;
    }
    if (stream->encoding != SPRITE_CELL_ENCODING_PACKBITS) {
        return false;
    }
    uint32_t filled = 0;
    while (filled < width) {
        if (stream->run_count > 0) {
            row[filled++] = stream->run_value;
            --stream->run_count;
            continue;
        }
        if (stream->literal_count > 0) {
            if (stream->remaining == 0) {
                return false;
            }
            row[filled++] = *stream->data++;
            --stream->remaining;
            --stream->literal_count;
            continue;
        }
        if (stream->remaining == 0) {
            return false;
        }
        const uint8_t control = *stream->data++;
        --stream->remaining;
        if (control < 128u) {
            stream->literal_count = (uint32_t)control + 1u;
        } else if (control == 128u) {
            /* PackBits no-op */
        } else {
            if (stream->remaining == 0) {
                return false;
            }
            stream->run_count = 257u - (uint32_t)control;
            stream->run_value = *stream->data++;
            --stream->remaining;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Blitting                                                           */

static void emit_row(
    const blit_ctx_t *ctx,
    const uint8_t *row,
    uint32_t width,
    int32_t native_x,
    int32_t native_y,
    bool flip_x)
{
    const sprite_atlas_t *atlas = ctx->atlas;
    const int32_t scale = atlas->scale;
    const int32_t sy0 = ctx->origin_y + native_y * scale;
    if (sy0 + scale <= 0 || sy0 >= SPRITE_FACE_HEIGHT) {
        return;
    }
    for (uint32_t column = 0; column < width; ++column) {
        const uint32_t source = flip_x ? width - 1u - column : column;
        const uint8_t index = row[source];
        if (index == atlas->transparent_index) {
            continue;
        }
        const uint16_t color = ctx->palette[index];
        const int32_t sx0 =
            ctx->origin_x + (native_x + (int32_t)column) * scale;
        for (int32_t dy = 0; dy < scale; ++dy) {
            const int32_t sy = sy0 + dy;
            if (sy < 0 || sy >= SPRITE_FACE_HEIGHT) {
                continue;
            }
            uint16_t *line = &ctx->pixels[sy * SPRITE_FACE_WIDTH];
            for (int32_t dx = 0; dx < scale; ++dx) {
                const int32_t sx = sx0 + dx;
                if (sx < 0 || sx >= SPRITE_FACE_WIDTH) {
                    continue;
                }
                line[sx] = color;
            }
        }
    }
}

static void blit_cell(
    const blit_ctx_t *ctx,
    uint16_t cell_index,
    int32_t native_x,
    int32_t native_y,
    bool flip_x)
{
    const sprite_atlas_t *atlas = ctx->atlas;
    if (cell_index == SPRITE_CELL_NONE ||
        cell_index >= atlas->cell_count) {
        return;
    }
    const sprite_cell_t *cell = &atlas->cells[cell_index];
    if (cell->width == 0 || cell->height == 0 ||
        cell->width > SPRITE_MAX_NATIVE_WIDTH) {
        return;
    }
    uint8_t row[SPRITE_MAX_NATIVE_WIDTH];
    cell_stream_t stream;
    stream_open(&stream, atlas, cell);
    const int32_t dst_x = native_x + cell->offset_x;
    for (uint32_t r = 0; r < cell->height; ++r) {
        if (!stream_next_row(&stream, row, cell->width)) {
            return;
        }
        emit_row(
            ctx, row, cell->width, dst_x,
            native_y + cell->offset_y + (int32_t)r, flip_x);
    }
}

/* ------------------------------------------------------------------ */
/* Deterministic idle motion                                          */

static uint8_t blink_envelope(
    const sprite_timing_t *timing, uint32_t position, uint32_t start)
{
    if (position < start) {
        return 0;
    }
    uint32_t delta = position - start;
    if (delta < timing->blink_close) {
        return (uint8_t)((delta * 255u) / timing->blink_close);
    }
    delta -= timing->blink_close;
    if (delta < timing->blink_hold) {
        return 255;
    }
    delta -= timing->blink_hold;
    if (delta < timing->blink_open) {
        return (uint8_t)(255u - (delta * 255u) / timing->blink_open);
    }
    return 0;
}

static uint8_t auto_blink_amount(
    const sprite_atlas_t *atlas, uint32_t clock)
{
    const sprite_timing_t *timing = &atlas->timing;
    if ((atlas->flags & SPRITE_ATLAS_FLAG_AUTO_BLINK) == 0 ||
        timing->blink_window == 0) {
        return 0;
    }
    const uint32_t total = (uint32_t)timing->blink_close +
        timing->blink_hold + timing->blink_open;
    if (total == 0 || total >= timing->blink_window) {
        return 0;
    }
    const uint32_t window = timing->blink_window;
    const uint32_t epoch = clock / window;
    const uint32_t position = clock % window;
    const uint32_t noise = hash_u32(epoch * 2654435761u + 0x5eedu);
    const uint32_t start = noise % (window - total);
    uint8_t amount = blink_envelope(timing, position, start);
    /* Occasional double blink, the classic acting touch. */
    if (((noise >> 13u) & 7u) == 0u) {
        const uint32_t start2 = start + total + 2400u;
        if (start2 + total < window) {
            amount = max_u8(
                amount, blink_envelope(timing, position, start2));
        }
    }
    /* Animator rule: blink when the eyes change direction. Some gaze
     * epochs open with a blink aligned to the saccade. */
    if (timing->gaze_window > total &&
        (atlas->flags & SPRITE_ATLAS_FLAG_IDLE_SACCADES) != 0) {
        const uint32_t gaze_epoch = clock / timing->gaze_window;
        if ((hash_u32(gaze_epoch + 0x9e3779b9u) & 7u) == 0u) {
            amount = max_u8(
                amount,
                blink_envelope(
                    timing, clock % timing->gaze_window, 0));
        }
    }
    return amount;
}

static uint8_t forced_blink_amount(
    const sprite_face_t *face, uint32_t clock)
{
    const sprite_timing_t *timing = &face->atlas->timing;
    const uint32_t delta = clock - face->forced_blink_edge;
    if (face->forced_blink) {
        if (timing->blink_close == 0 || delta >= timing->blink_close) {
            return 255;
        }
        return (uint8_t)((delta * 255u) / timing->blink_close);
    }
    if (timing->blink_open == 0 || delta >= timing->blink_open) {
        return 0;
    }
    return (uint8_t)(255u - (delta * 255u) / timing->blink_open);
}

static void saccade_offsets(
    const sprite_atlas_t *atlas, uint32_t clock, sprite_motion_t *motion)
{
    motion->saccade_x = 0;
    motion->saccade_y = 0;
    const sprite_timing_t *timing = &atlas->timing;
    if ((atlas->flags & SPRITE_ATLAS_FLAG_IDLE_SACCADES) == 0 ||
        timing->gaze_window == 0) {
        return;
    }
    const uint32_t epoch = clock / timing->gaze_window;
    const uint32_t position = clock % timing->gaze_window;
    const uint32_t current = hash_u32(epoch + 0x51ed270bu);
    const uint32_t previous =
        hash_u32((epoch == 0 ? 0 : epoch - 1u) + 0x51ed270bu);
    const int32_t current_x = (int32_t)((current >> 3u) % 5u) - 2;
    const int32_t current_y = (int32_t)((current >> 11u) % 3u) - 1;
    const int32_t previous_x = (int32_t)((previous >> 3u) % 5u) - 2;
    const int32_t previous_y = (int32_t)((previous >> 11u) % 3u) - 1;
    const int32_t amount = position < SPRITE_SACCADE_MOVE
        ? (int32_t)((position * 255u) / SPRITE_SACCADE_MOVE)
        : 255;
    motion->saccade_x = (int16_t)(
        previous_x + ((current_x - previous_x) * amount) / 255);
    motion->saccade_y = (int16_t)(
        previous_y + ((current_y - previous_y) * amount) / 255);
}

static void compute_motion(
    sprite_face_t *face,
    const face_keyframe_t *keyframe,
    uint32_t clock,
    sprite_motion_t *motion)
{
    const sprite_atlas_t *atlas = face->atlas;

    /* Forced blink edges from the keyframe flag. */
    const uint8_t blinking =
        (keyframe->flags & FACE_KEYFRAME_FLAG_BLINKING) != 0u;
    const uint8_t was_blinking =
        (face->prev_flags & FACE_KEYFRAME_FLAG_BLINKING) != 0u;
    if (blinking != was_blinking) {
        face->forced_blink = blinking;
        face->forced_blink_edge = clock;
    }
    motion->blink = max_u8(
        auto_blink_amount(atlas, clock),
        forced_blink_amount(face, clock));

    saccade_offsets(atlas, clock, motion);

    motion->bob = 0;
    if ((atlas->flags & SPRITE_ATLAS_FLAG_BREATHE) != 0 &&
        atlas->timing.breathe_period != 0) {
        const uint32_t phase = clock % atlas->timing.breathe_period;
        motion->bob = (int16_t)(
            (phase * 2u) / atlas->timing.breathe_period);
    }
}

/* ------------------------------------------------------------------ */
/* Mouth selection, debounce, coarticulation                          */

uint8_t sprite_face_select_shape(
    const sprite_selector_t *selector, const face_keyframe_t *keyframe)
{
    if (keyframe->mouth_open < selector->open_min) {
        return keyframe->mouth_press >= selector->press_min
            ? SPRITE_MOUTH_A
            : SPRITE_MOUTH_X;
    }
    if (keyframe->mouth_teeth >= selector->teeth_min) {
        if (keyframe->mouth_open < selector->teeth_open) {
            return SPRITE_MOUTH_G;
        }
        return keyframe->mouth_round >= selector->teeth_round
            ? SPRITE_MOUTH_H
            : SPRITE_MOUTH_B;
    }
    if (keyframe->mouth_round >= selector->round_min) {
        return keyframe->mouth_open >= selector->round_open
            ? SPRITE_MOUTH_E
            : SPRITE_MOUTH_F;
    }
    if (keyframe->mouth_width >= selector->wide_min) {
        return keyframe->mouth_open >= selector->wide_open
            ? SPRITE_MOUTH_D
            : SPRITE_MOUTH_B;
    }
    if (keyframe->mouth_open >= selector->open_wide) {
        return SPRITE_MOUTH_D;
    }
    return SPRITE_MOUTH_C;
}

/* Rhubarb's in-between rules: A/B/X-to-D animates through C, and
 * C/D-to-F through E (and the reverse paths likewise), so wide jaw
 * jumps and pucker snaps get one classic intermediate drawing. */
static uint8_t transition_via(uint8_t from, uint8_t to)
{
    const bool from_closed = from == SPRITE_MOUTH_X ||
        from == SPRITE_MOUTH_A || from == SPRITE_MOUTH_B ||
        from == SPRITE_MOUTH_G;
    const bool to_closed = to == SPRITE_MOUTH_X ||
        to == SPRITE_MOUTH_A || to == SPRITE_MOUTH_B ||
        to == SPRITE_MOUTH_G;
    if (from == SPRITE_MOUTH_D && to_closed) {
        return SPRITE_MOUTH_C;
    }
    if (to == SPRITE_MOUTH_D && from_closed) {
        return SPRITE_MOUTH_C;
    }
    if ((from == SPRITE_MOUTH_C || from == SPRITE_MOUTH_D) &&
        to == SPRITE_MOUTH_F) {
        return SPRITE_MOUTH_E;
    }
    if (from == SPRITE_MOUTH_F &&
        (to == SPRITE_MOUTH_C || to == SPRITE_MOUTH_D)) {
        return SPRITE_MOUTH_E;
    }
    return to;
}

static void update_mouth(
    sprite_face_t *face, const face_keyframe_t *keyframe, uint32_t clock)
{
    const sprite_atlas_t *atlas = face->atlas;
    const uint8_t target =
        sprite_face_select_shape(&atlas->selector, keyframe);
    if (target != face->last_target) {
        face->last_target = target;
        face->target_since = clock;
    }
    if (target == face->current_shape) {
        return;
    }
    if (clock - face->shape_since < atlas->timing.mouth_min_hold) {
        return;
    }
    /* Rhubarb pause rule: a pause shorter than the close delay keeps
     * the previous shape instead of snapping the mouth shut. */
    if (target == SPRITE_MOUTH_X &&
        clock - face->target_since < atlas->timing.mouth_close_delay) {
        return;
    }
    face->current_shape = transition_via(face->current_shape, target);
    face->shape_since = clock;
}

/* ------------------------------------------------------------------ */
/* Layer drawing                                                      */

static uint16_t pick_indexed_cell(
    const uint16_t *cells, uint16_t count, uint32_t numerator,
    uint32_t denominator)
{
    if (count == 0) {
        return SPRITE_CELL_NONE;
    }
    uint32_t index =
        (numerator * (count - 1u) + denominator / 2u) / denominator;
    if (index >= count) {
        index = count - 1u;
    }
    return cells[index];
}

static void draw_eye(
    const blit_ctx_t *ctx,
    const sprite_eye_slot_t *slot,
    uint8_t keyframe_open,
    const sprite_motion_t *motion)
{
    if (slot->cell_count == 0) {
        return;
    }
    const uint8_t open = mul_u8(keyframe_open, 255u - motion->blink);
    const uint16_t cell = pick_indexed_cell(
        slot->cells, slot->cell_count, 255u - open, 255u);
    blit_cell(
        ctx, cell, slot->x, slot->y + motion->bob,
        (slot->flags & SPRITE_SLOT_FLIP_X) != 0);
}

static void draw_pupil(
    const blit_ctx_t *ctx,
    const sprite_pupil_slot_t *slot,
    const sprite_eye_slot_t *eye,
    uint8_t keyframe_open,
    int8_t look_x,
    int8_t look_y,
    const sprite_motion_t *motion)
{
    if (slot->cell == SPRITE_CELL_NONE) {
        return;
    }
    const uint8_t open = mul_u8(keyframe_open, 255u - motion->blink);
    if (open < 48u) {
        return;
    }
    if (eye->cell_count > 1) {
        const uint32_t lid =
            ((255u - open) * (eye->cell_count - 1u) + 127u) / 255u;
        if (lid >= eye->cell_count - 1u) {
            return;
        }
    }
    const int32_t gaze_x =
        ((int32_t)look_x * slot->range_x) / 127 + motion->saccade_x;
    const int32_t gaze_y =
        ((int32_t)look_y * slot->range_y) / 127 + motion->saccade_y;
    const int32_t x = clamp_i32(
        slot->x + gaze_x, slot->min_x, slot->max_x);
    const int32_t y = clamp_i32(
        slot->y + gaze_y, slot->min_y, slot->max_y);
    blit_cell(ctx, slot->cell, x, y + motion->bob, false);
}

static void draw_brow(
    const blit_ctx_t *ctx,
    const sprite_brow_slot_t *slot,
    int8_t brow,
    const sprite_motion_t *motion)
{
    if (slot->cell_count == 0) {
        return;
    }
    const uint16_t cell = pick_indexed_cell(
        slot->cells, slot->cell_count, (uint32_t)(brow + 128), 255u);
    const int32_t lift = ((int32_t)brow * slot->max_lift) / 127;
    blit_cell(
        ctx, cell, slot->x, slot->y - lift + motion->bob,
        (slot->flags & SPRITE_SLOT_FLIP_X) != 0);
}

static void draw_overlay(
    const blit_ctx_t *ctx,
    const face_keyframe_t *keyframe,
    uint32_t clock,
    const sprite_motion_t *motion)
{
    const sprite_atlas_t *atlas = ctx->atlas;
    const sprite_timing_t *timing = &atlas->timing;
    if (atlas->sequence_count == 0 || timing->idle_window == 0) {
        return;
    }
    const uint32_t epoch = clock / timing->idle_window;
    const uint32_t position = clock % timing->idle_window;
    const uint32_t noise = hash_u32(epoch + 0xacedu);
    if ((noise % 100u) >= 45u) {
        return;
    }
    const sprite_sequence_t *sequence =
        &atlas->sequences[(noise >> 7u) % atlas->sequence_count];
    const bool speaking =
        (keyframe->flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0u;
    if (speaking && (sequence->flags & SPRITE_SEQ_WHILE_SPEAKING) == 0) {
        return;
    }
    uint32_t total = 0;
    for (uint16_t i = 0; i < sequence->frame_count; ++i) {
        total += sequence->frames[i].duration;
    }
    if (total == 0 || total >= timing->idle_window) {
        return;
    }
    const uint32_t start =
        (noise >> 11u) % (timing->idle_window - total);
    if (position < start || position >= start + total) {
        return;
    }
    uint32_t offset = position - start;
    for (uint16_t i = 0; i < sequence->frame_count; ++i) {
        const sprite_seq_frame_t *frame = &sequence->frames[i];
        if (offset < frame->duration) {
            blit_cell(
                ctx, frame->cell, frame->x,
                frame->y + motion->bob, false);
            return;
        }
        offset -= frame->duration;
    }
}

/* ------------------------------------------------------------------ */
/* Validation                                                         */

static bool validate_cell(
    const sprite_atlas_t *atlas, const sprite_cell_t *cell)
{
    if (cell->width == 0 || cell->height == 0 ||
        cell->width > SPRITE_MAX_NATIVE_WIDTH) {
        return false;
    }
    if (cell->data_offset > atlas->blob_size ||
        cell->data_length > atlas->blob_size - cell->data_offset) {
        return false;
    }
    uint8_t row[SPRITE_MAX_NATIVE_WIDTH];
    cell_stream_t stream;
    stream_open(&stream, atlas, cell);
    for (uint32_t r = 0; r < cell->height; ++r) {
        if (!stream_next_row(&stream, row, cell->width)) {
            return false;
        }
        for (uint32_t c = 0; c < cell->width; ++c) {
            if (row[c] >= atlas->palette_count) {
                return false;
            }
        }
    }
    return true;
}

static bool validate_cell_ref(const sprite_atlas_t *atlas, uint16_t cell)
{
    return cell == SPRITE_CELL_NONE || cell < atlas->cell_count;
}

static bool validate_bank(
    const sprite_atlas_t *atlas, const sprite_bank_t *bank)
{
    if (bank->base_cell != SPRITE_CELL_NONE &&
        bank->base_cell >= atlas->cell_count) {
        return false;
    }
    for (uint32_t i = 0; i < SPRITE_MOUTH_SHAPE_COUNT; ++i) {
        if (!validate_cell_ref(atlas, bank->mouth.cells[i])) {
            return false;
        }
    }
    const sprite_eye_slot_t *eyes[2] = {
        &bank->eye_left, &bank->eye_right,
    };
    for (uint32_t e = 0; e < 2u; ++e) {
        if (eyes[e]->cell_count > SPRITE_MAX_LID_CELLS) {
            return false;
        }
        for (uint32_t i = 0; i < eyes[e]->cell_count; ++i) {
            if (!validate_cell_ref(atlas, eyes[e]->cells[i])) {
                return false;
            }
        }
    }
    const sprite_pupil_slot_t *pupils[2] = {
        &bank->pupil_left, &bank->pupil_right,
    };
    for (uint32_t p = 0; p < 2u; ++p) {
        if (!validate_cell_ref(atlas, pupils[p]->cell)) {
            return false;
        }
        if (pupils[p]->min_x > pupils[p]->max_x ||
            pupils[p]->min_y > pupils[p]->max_y) {
            return false;
        }
    }
    const sprite_brow_slot_t *brows[2] = {
        &bank->brow_left, &bank->brow_right,
    };
    for (uint32_t b = 0; b < 2u; ++b) {
        if (brows[b]->cell_count > SPRITE_MAX_BROW_CELLS) {
            return false;
        }
        for (uint32_t i = 0; i < brows[b]->cell_count; ++i) {
            if (!validate_cell_ref(atlas, brows[b]->cells[i])) {
                return false;
            }
        }
    }
    return true;
}

static bool validate_atlas(const sprite_atlas_t *atlas)
{
    if (atlas->magic != SPRITE_SHEET_MAGIC ||
        atlas->version != SPRITE_SHEET_VERSION) {
        return false;
    }
    if (atlas->scale != 1 && atlas->scale != 2) {
        return false;
    }
    if (atlas->native_width == 0 || atlas->native_height == 0 ||
        (int32_t)atlas->native_width * atlas->scale >
            SPRITE_FACE_WIDTH ||
        (int32_t)atlas->native_height * atlas->scale >
            SPRITE_FACE_HEIGHT) {
        return false;
    }
    if (atlas->palette_count == 0 ||
        atlas->palette_count > SPRITE_MAX_PALETTE ||
        atlas->palette == NULL) {
        return false;
    }
    if (atlas->cell_count == 0 || atlas->cells == NULL ||
        atlas->blob == NULL) {
        return false;
    }
    if (atlas->bank_count == 0 || atlas->banks == NULL) {
        return false;
    }
    if (atlas->sequence_count > 0 && atlas->sequences == NULL) {
        return false;
    }
    if (atlas->cycle_count > SPRITE_MAX_CYCLES) {
        return false;
    }
    if (atlas->cycle_count > 0 && atlas->cycles == NULL) {
        return false;
    }
    for (uint16_t i = 0; i < atlas->cell_count; ++i) {
        if (!validate_cell(atlas, &atlas->cells[i])) {
            return false;
        }
    }
    for (uint8_t i = 0; i < atlas->bank_count; ++i) {
        if (!validate_bank(atlas, &atlas->banks[i])) {
            return false;
        }
    }
    for (uint8_t i = 0; i < atlas->sequence_count; ++i) {
        const sprite_sequence_t *sequence = &atlas->sequences[i];
        if (sequence->frame_count == 0 || sequence->frames == NULL) {
            return false;
        }
        for (uint16_t f = 0; f < sequence->frame_count; ++f) {
            if (!validate_cell_ref(atlas, sequence->frames[f].cell)) {
                return false;
            }
        }
    }
    for (uint8_t i = 0; i < atlas->cycle_count; ++i) {
        const sprite_cycle_t *cycle = &atlas->cycles[i];
        if (cycle->count < 2 || cycle->period == 0 ||
            (uint32_t)cycle->first + cycle->count >
                atlas->palette_count) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */

static void reset_state(sprite_face_t *face)
{
    const sprite_atlas_t *atlas = face->atlas;
    face->last_clock = 0;
    face->shape_since = 0;
    face->target_since = 0;
    face->current_shape = SPRITE_MOUTH_X;
    face->last_target = SPRITE_MOUTH_X;
    face->prev_flags = 0;
    face->forced_blink = 0;
    /* Backdate the forced-blink edge so a fresh engine renders open
     * eyes at clock zero instead of finishing a phantom reopen ramp. */
    face->forced_blink_edge = 0u - (uint32_t)atlas->timing.blink_open;
}

bool sprite_face_init(sprite_face_t *face, const sprite_atlas_t *atlas)
{
    if (face == NULL || atlas == NULL) {
        return false;
    }
    memset(face, 0, sizeof(*face));
    if (!validate_atlas(atlas)) {
        return false;
    }
    face->atlas = atlas;
    reset_state(face);
    return true;
}

bool sprite_face_render(
    sprite_face_t *face,
    const face_keyframe_t *keyframe,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if (face == NULL || face->atlas == NULL || keyframe == NULL ||
        rgb565 == NULL || pixel_capacity < SPRITE_FACE_PIXEL_COUNT) {
        return false;
    }
    const sprite_atlas_t *atlas = face->atlas;
    if (sample_clock < face->last_clock) {
        reset_state(face);
    }
    face->last_clock = sample_clock;

    sprite_motion_t motion;
    compute_motion(face, keyframe, sample_clock, &motion);
    update_mouth(face, keyframe, sample_clock);
    face->prev_flags = keyframe->flags;

    /* Effective palette, with any classic colour cycling applied. */
    uint16_t cycled[SPRITE_MAX_PALETTE];
    const uint16_t *palette = atlas->palette;
    if (atlas->cycle_count > 0) {
        memcpy(
            cycled, atlas->palette,
            (size_t)atlas->palette_count * sizeof(uint16_t));
        for (uint8_t i = 0; i < atlas->cycle_count; ++i) {
            const sprite_cycle_t *cycle = &atlas->cycles[i];
            const uint32_t shift =
                (sample_clock / cycle->period) % cycle->count;
            for (uint32_t slot = 0; slot < cycle->count; ++slot) {
                cycled[cycle->first + slot] = atlas->palette[
                    cycle->first + (slot + shift) % cycle->count];
            }
        }
        palette = cycled;
    }

    for (int32_t i = 0; i < SPRITE_FACE_PIXEL_COUNT; ++i) {
        rgb565[i] = atlas->background;
    }

    const blit_ctx_t ctx = {
        .pixels = rgb565,
        .atlas = atlas,
        .palette = palette,
        .origin_x = (SPRITE_FACE_WIDTH -
                     (int32_t)atlas->native_width * atlas->scale) / 2,
        .origin_y = (SPRITE_FACE_HEIGHT -
                     (int32_t)atlas->native_height * atlas->scale) / 2,
    };

    const uint8_t bank_index =
        keyframe->expression < atlas->bank_count
            ? keyframe->expression
            : 0u;
    const sprite_bank_t *bank = &atlas->banks[bank_index];

    blit_cell(&ctx, bank->base_cell, 0, motion.bob, false);
    draw_brow(&ctx, &bank->brow_left, keyframe->brow, &motion);
    draw_brow(&ctx, &bank->brow_right, keyframe->brow, &motion);
    draw_eye(&ctx, &bank->eye_left, keyframe->eye_left_open, &motion);
    draw_eye(&ctx, &bank->eye_right, keyframe->eye_right_open, &motion);
    draw_pupil(
        &ctx, &bank->pupil_left, &bank->eye_left,
        keyframe->eye_left_open, keyframe->look_x, keyframe->look_y,
        &motion);
    draw_pupil(
        &ctx, &bank->pupil_right, &bank->eye_right,
        keyframe->eye_right_open, keyframe->look_x, keyframe->look_y,
        &motion);
    blit_cell(
        &ctx, bank->mouth.cells[face->current_shape],
        bank->mouth.x, bank->mouth.y + motion.bob, false);
    draw_overlay(&ctx, keyframe, sample_clock, &motion);
    return true;
}
