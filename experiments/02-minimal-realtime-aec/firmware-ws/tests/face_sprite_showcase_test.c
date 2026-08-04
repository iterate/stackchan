#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face_sprite_showcase.h"

enum {
    SHOWCASE_COUNT = 2,
    EXPRESSION_COUNT = 11,
    MOUTH_COUNT = 23,
    CONTACT_GAP = 2,
};

typedef struct {
    const char *name;
    int8_t valence;
    uint8_t arousal;
    int8_t corner;
    int8_t brow_inner;
    uint8_t squint;
} expression_case_t;

static const expression_case_t EXPRESSIONS[EXPRESSION_COUNT] = {
    { "neutral", 0, 72, 0, 0, 0 },
    { "warm", 52, 112, 36, 10, 20 },
    { "joy", 94, 184, 78, 22, 68 },
    { "concern", -52, 126, -24, 58, 14 },
    { "surprise", 18, 232, 8, 78, 0 },
    { "thoughtful", 4, 92, -6, 18, 22 },
    { "skeptical", -18, 104, -12, -14, 42 },
    { "determined", 12, 166, -8, -34, 54 },
    { "sleepy", 8, 34, 4, -26, 118 },
    { "excited", 86, 250, 62, 52, 14 },
    { "embarrassed", 28, 176, 24, 24, 116 },
};

static const uint8_t MOUTH_ROLES[MOUTH_COUNT] = {
    FACE_SPRITE_MOUTH_REST,
    FACE_SPRITE_MOUTH_PRESS,
    FACE_SPRITE_MOUTH_LIP_BITE,
    FACE_SPRITE_MOUTH_TONGUE,
    FACE_SPRITE_MOUTH_HALF,
    FACE_SPRITE_MOUTH_HALF,
    FACE_SPRITE_MOUTH_ROUND,
    FACE_SPRITE_MOUTH_TEETH,
    FACE_SPRITE_MOUTH_PRESS,
    FACE_SPRITE_MOUTH_ROUND,
    FACE_SPRITE_MOUTH_WIDE,
    FACE_SPRITE_MOUTH_WIDE,
    FACE_SPRITE_MOUTH_HALF,
    FACE_SPRITE_MOUTH_ROUND,
    FACE_SPRITE_MOUTH_PUCKER,
    FACE_SPRITE_MOUTH_WIDE,
    FACE_SPRITE_MOUTH_HALF,
    FACE_SPRITE_MOUTH_TEETH,
    FACE_SPRITE_MOUTH_TONGUE,
    FACE_SPRITE_MOUTH_PUCKER,
    FACE_SPRITE_MOUTH_ROUND,
    FACE_SPRITE_MOUTH_HALF,
    FACE_SPRITE_MOUTH_HALF,
};

typedef struct {
    uint32_t before[8];
    uint16_t pixels[FACE_RENDER_PIXEL_COUNT];
    uint32_t after[8];
} guarded_frame_t;

static void reset_guard(guarded_frame_t *frame)
{
    for (size_t index = 0U; index < 8U; ++index) {
        frame->before[index] = 0x5aa50ff0U ^ (uint32_t)index;
        frame->after[index] = 0xc33cc00cU ^ (uint32_t)index;
    }
    memset(frame->pixels, 0xa5, sizeof(frame->pixels));
}

static void assert_guard(const guarded_frame_t *frame)
{
    for (size_t index = 0U; index < 8U; ++index) {
        assert(frame->before[index] ==
            (0x5aa50ff0U ^ (uint32_t)index));
        assert(frame->after[index] ==
            (0xc33cc00cU ^ (uint32_t)index));
    }
}

static uint32_t hash_frame(
    const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_RENDER_PIXEL_COUNT;
         ++index) {
        hash ^= (uint8_t)(pixels[index] & 0xffU);
        hash *= 16777619U;
        hash ^= (uint8_t)(pixels[index] >> 8U);
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t fold_hash(
    uint32_t aggregate,
    uint32_t value)
{
    aggregate ^= value;
    aggregate *= 16777619U;
    return aggregate;
}

static size_t unique_count(
    const uint32_t *values,
    size_t count)
{
    size_t unique = 0U;
    for (size_t index = 0U; index < count; ++index) {
        bool seen = false;
        for (size_t earlier = 0U; earlier < index; ++earlier) {
            if (values[earlier] == values[index]) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            ++unique;
        }
    }
    return unique;
}

static face_render_key_t expression_key(size_t index)
{
    assert(index < EXPRESSION_COUNT);
    const expression_case_t *item = &EXPRESSIONS[index];
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 208U;
    key.controls.mouth_width = 196U;
    key.controls.mouth_round = 30U;
    key.controls.eye_left_open = 255U;
    key.controls.eye_right_open = 255U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = 10U;
    key.viseme_weight = 255U;
    key.audio_level = 180U;
    key.viseme_set = FACE_VISEME_SET_MICROSOFT22;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = item->corner;
    key.mouth_corner_right = item->corner;
    key.eye_left_squint = item->squint;
    key.eye_right_squint = item->squint;
    key.brow_inner = item->brow_inner;
    key.brow_outer_left = item->brow_inner / 2;
    key.brow_outer_right = item->brow_inner / 2;
    key.affect_valence = item->valence;
    key.affect_arousal = item->arousal;
    key.expression_weight = 255U;
    key.attention = 224U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static face_render_key_t mouth_key(size_t slot)
{
    assert(slot < MOUTH_COUNT);
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 210U;
    key.controls.mouth_width = 190U;
    key.controls.eye_left_open = 255U;
    key.controls.eye_right_open = 255U;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme_weight = 255U;
    key.audio_level = 170U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 72U;
    key.attention = 220U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    if (slot < 22U) {
        key.viseme_set = FACE_VISEME_SET_MICROSOFT22;
        key.viseme = (uint8_t)slot;
    } else {
        key.viseme_set = FACE_VISEME_SET_CUSTOM;
        key.viseme = 42U;
    }
    return key;
}

static uint32_t render_checked(
    const face_sprite_player_t *player,
    const face_render_key_t *key,
    uint32_t sample_clock,
    guarded_frame_t *frame)
{
    reset_guard(frame);
    const bool rendered = face_sprite_render_snapshot(
        player,
        key,
        sample_clock,
        frame->pixels,
        FACE_RENDER_PIXEL_COUNT);
    if (!rendered) {
        fprintf(
            stderr,
            "render failed: atlas=%s clock=%u set=%u viseme=%u "
            "expression_weight=%u valence=%d arousal=%u\n",
            player != NULL && player->atlas != NULL
                ? player->atlas->name
                : "(null)",
            (unsigned)sample_clock,
            (unsigned)key->viseme_set,
            (unsigned)key->viseme,
            (unsigned)key->expression_weight,
            (int)key->affect_valence,
            (unsigned)key->affect_arousal);
    }
    assert(rendered);
    assert_guard(frame);
    return hash_frame(frame->pixels);
}

static void assert_clear_outer_edge(
    const guarded_frame_t *frame,
    uint16_t background)
{
    for (size_t x = 0U; x < FACE_RENDER_WIDTH; ++x) {
        assert(frame->pixels[x] == background);
        assert(frame->pixels[
            (FACE_RENDER_HEIGHT - 1U) * FACE_RENDER_WIDTH + x] ==
            background);
    }
    for (size_t y = 0U; y < FACE_RENDER_HEIGHT; ++y) {
        assert(frame->pixels[y * FACE_RENDER_WIDTH] == background);
        assert(frame->pixels[
            y * FACE_RENDER_WIDTH + FACE_RENDER_WIDTH - 1U] ==
            background);
    }
}

static void rgb565_to_rgb888(
    uint16_t value,
    uint8_t rgb[3])
{
    const uint8_t red = (uint8_t)((value >> 11U) & 31U);
    const uint8_t green = (uint8_t)((value >> 5U) & 63U);
    const uint8_t blue = (uint8_t)(value & 31U);
    rgb[0] = (uint8_t)((red << 3U) | (red >> 2U));
    rgb[1] = (uint8_t)((green << 2U) | (green >> 4U));
    rgb[2] = (uint8_t)((blue << 3U) | (blue >> 2U));
}

static void copy_frame(
    uint16_t *sheet,
    size_t sheet_width,
    size_t sheet_height,
    size_t destination_x,
    size_t destination_y,
    const uint16_t *frame)
{
    assert(destination_x + FACE_RENDER_WIDTH <= sheet_width);
    assert(destination_y + FACE_RENDER_HEIGHT <= sheet_height);
    for (size_t y = 0U; y < FACE_RENDER_HEIGHT; ++y) {
        memcpy(
            &sheet[(destination_y + y) * sheet_width + destination_x],
            &frame[y * FACE_RENDER_WIDTH],
            FACE_RENDER_WIDTH * sizeof(uint16_t));
    }
}

static void frame_rect(
    uint16_t *sheet,
    size_t sheet_width,
    size_t x,
    size_t y,
    size_t width,
    size_t height,
    uint16_t color)
{
    for (size_t column = x; column < x + width; ++column) {
        sheet[y * sheet_width + column] = color;
        sheet[(y + height - 1U) * sheet_width + column] = color;
    }
    for (size_t row = y; row < y + height; ++row) {
        sheet[row * sheet_width + x] = color;
        sheet[row * sheet_width + x + width - 1U] = color;
    }
}

static bool write_ppm(
    const char *path,
    const uint16_t *pixels,
    size_t width,
    size_t height)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (fprintf(file, "P6\n%zu %zu\n255\n", width, height) < 0) {
        fclose(file);
        return false;
    }
    for (size_t pixel = 0U; pixel < width * height; ++pixel) {
        uint8_t rgb[3];
        rgb565_to_rgb888(pixels[pixel], rgb);
        if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
            fclose(file);
            return false;
        }
    }
    return fclose(file) == 0;
}

static uint16_t expression_border(size_t expression)
{
    static const uint16_t COLORS[EXPRESSION_COUNT] = {
        0x8410U, 0xfdb0U, 0xffe0U, 0x64dfU, 0xf81fU, 0x7befU,
        0xa145U, 0xf980U, 0x4208U, 0x07ffU, 0xfbb7U,
    };
    return COLORS[expression];
}

static bool write_expression_contact(
    const char *path,
    const face_sprite_player_t players[SHOWCASE_COUNT])
{
    const size_t width =
        EXPRESSION_COUNT * FACE_RENDER_WIDTH +
        (EXPRESSION_COUNT + 1U) * CONTACT_GAP;
    const size_t height =
        SHOWCASE_COUNT * FACE_RENDER_HEIGHT +
        (SHOWCASE_COUNT + 1U) * CONTACT_GAP;
    uint16_t *sheet = calloc(width * height, sizeof(*sheet));
    if (sheet == NULL) {
        return false;
    }
    guarded_frame_t *frame = malloc(sizeof(*frame));
    if (frame == NULL) {
        free(sheet);
        return false;
    }
    for (size_t showcase = 0U; showcase < SHOWCASE_COUNT; ++showcase) {
        for (size_t expression = 0U;
             expression < EXPRESSION_COUNT;
             ++expression) {
            const face_render_key_t key = expression_key(expression);
            (void)render_checked(
                &players[showcase], &key, 16000U, frame);
            const size_t x =
                CONTACT_GAP +
                expression * (FACE_RENDER_WIDTH + CONTACT_GAP);
            const size_t y =
                CONTACT_GAP +
                showcase * (FACE_RENDER_HEIGHT + CONTACT_GAP);
            copy_frame(
                sheet, width, height, x, y, frame->pixels);
            frame_rect(
                sheet,
                width,
                x,
                y,
                FACE_RENDER_WIDTH,
                FACE_RENDER_HEIGHT,
                expression_border(expression));
        }
    }
    const bool result = write_ppm(path, sheet, width, height);
    free(frame);
    free(sheet);
    return result;
}

static bool write_mouth_contact(
    const char *path,
    const face_sprite_player_t players[SHOWCASE_COUNT])
{
    enum { COLUMNS = 12, ROWS_PER_SHOWCASE = 2 };
    const size_t rows = SHOWCASE_COUNT * ROWS_PER_SHOWCASE;
    const size_t width =
        COLUMNS * FACE_RENDER_WIDTH +
        (COLUMNS + 1U) * CONTACT_GAP;
    const size_t height =
        rows * FACE_RENDER_HEIGHT +
        (rows + 1U) * CONTACT_GAP;
    uint16_t *sheet = calloc(width * height, sizeof(*sheet));
    if (sheet == NULL) {
        return false;
    }
    guarded_frame_t *frame = malloc(sizeof(*frame));
    if (frame == NULL) {
        free(sheet);
        return false;
    }
    for (size_t showcase = 0U; showcase < SHOWCASE_COUNT; ++showcase) {
        for (size_t mouth = 0U; mouth < MOUTH_COUNT; ++mouth) {
            const face_render_key_t key = mouth_key(mouth);
            (void)render_checked(
                &players[showcase], &key, 8000U, frame);
            const size_t column = mouth % COLUMNS;
            const size_t row =
                showcase * ROWS_PER_SHOWCASE + mouth / COLUMNS;
            const size_t x =
                CONTACT_GAP +
                column * (FACE_RENDER_WIDTH + CONTACT_GAP);
            const size_t y =
                CONTACT_GAP +
                row * (FACE_RENDER_HEIGHT + CONTACT_GAP);
            copy_frame(
                sheet, width, height, x, y, frame->pixels);
            frame_rect(
                sheet,
                width,
                x,
                y,
                FACE_RENDER_WIDTH,
                FACE_RENDER_HEIGHT,
                (uint16_t)(0x4208U + mouth * 0x0201U));
        }
    }
    const bool result = write_ppm(path, sheet, width, height);
    free(frame);
    free(sheet);
    return result;
}

int main(int argc, char **argv)
{
    assert(face_sprite_showcase_count() == SHOWCASE_COUNT);
    assert(face_sprite_showcase_info(SHOWCASE_COUNT) == NULL);

    face_sprite_player_t players[SHOWCASE_COUNT];
    guarded_frame_t *frame = malloc(sizeof(*frame));
    assert(frame != NULL);
    const uint32_t expected_blob[SHOWCASE_COUNT] = {
        11238U, 8435U,
    };
    const uint32_t expected_declared[SHOWCASE_COUNT] = {
        15534U, 12431U,
    };
    const char *expected_slugs[SHOWCASE_COUNT] = {
        "vga_star_navigator",
        "pocket_relay_creature",
    };
    for (size_t showcase = 0U; showcase < SHOWCASE_COUNT; ++showcase) {
        const face_sprite_showcase_info_t *info =
            face_sprite_showcase_info(showcase);
        assert(info != NULL);
        assert(strcmp(info->slug, expected_slugs[showcase]) == 0);
        assert(info->atlas != NULL);
        assert(info->atlas->native_width == 80U);
        assert(info->atlas->native_height == 60U);
        assert(info->atlas->scale == 2U);
        assert(info->atlas->bank_count == EXPRESSION_COUNT);
        assert(info->atlas->mouth_slot_count == MOUTH_COUNT);
        assert(info->atlas->viseme_map_count >= 51U);
        assert(
            (info->atlas->flags &
             FACE_SPRITE_ATLAS_ALLOW_CLIPPING) == 0U);
        assert(info->expression_banks == EXPRESSION_COUNT);
        assert(info->mouth_slots == MOUTH_COUNT);
        assert(info->encoded_pixel_bytes == expected_blob[showcase]);
        assert(info->portable_payload_bytes ==
            expected_declared[showcase]);
        assert(info->cell_count == info->atlas->cell_count);
        assert(info->palette_colors == info->atlas->palette_count);
        assert(face_sprite_player_init(
            &players[showcase], info->atlas));
        const face_render_key_t neutral = expression_key(0U);
        (void)render_checked(
            &players[showcase], &neutral, 33600U, frame);
        assert_clear_outer_edge(frame, info->atlas->background);
    }

    uint32_t expression_aggregate[SHOWCASE_COUNT] = {
        2166136261U, 2166136261U,
    };
    uint32_t mouth_aggregate[SHOWCASE_COUNT] = {
        2166136261U, 2166136261U,
    };
    for (size_t showcase = 0U; showcase < SHOWCASE_COUNT; ++showcase) {
        uint32_t hashes[EXPRESSION_COUNT];
        for (size_t expression = 0U;
             expression < EXPRESSION_COUNT;
             ++expression) {
            const face_render_key_t key = expression_key(expression);
            hashes[expression] = render_checked(
                &players[showcase], &key, 16000U, frame);
            expression_aggregate[showcase] = fold_hash(
                expression_aggregate[showcase],
                hashes[expression]);
        }
        assert(unique_count(hashes, EXPRESSION_COUNT) ==
            EXPRESSION_COUNT);

        uint32_t mouth_hashes[MOUTH_COUNT];
        for (size_t mouth = 0U; mouth < MOUTH_COUNT; ++mouth) {
            const face_render_key_t key = mouth_key(mouth);
            uint8_t role = FACE_SPRITE_MOUTH_SLOT_NONE;
            const uint8_t selected = face_sprite_select_mouth_slot(
                players[showcase].atlas, &key, &role);
            assert(selected == mouth);
            assert(role == MOUTH_ROLES[mouth] ||
                role == FACE_SPRITE_MOUTH_SLOT_NONE);
            mouth_hashes[mouth] = render_checked(
                &players[showcase], &key, 8000U, frame);
            mouth_aggregate[showcase] = fold_hash(
                mouth_aggregate[showcase], mouth_hashes[mouth]);
        }
        assert(unique_count(mouth_hashes, MOUTH_COUNT) >= 20U);
    }
    static const uint32_t GOLDEN_EXPRESSIONS[SHOWCASE_COUNT] = {
        0x749336feU, 0xfd07646eU,
    };
    static const uint32_t GOLDEN_MOUTHS[SHOWCASE_COUNT] = {
        0xca8d6462U, 0xb55cd94aU,
    };
    for (size_t showcase = 0U; showcase < SHOWCASE_COUNT; ++showcase) {
        if (expression_aggregate[showcase] !=
                GOLDEN_EXPRESSIONS[showcase] ||
            mouth_aggregate[showcase] != GOLDEN_MOUTHS[showcase]) {
            fprintf(
                stderr,
                "golden mismatch[%zu]: expressions=%08x mouths=%08x\n",
                showcase,
                expression_aggregate[showcase],
                mouth_aggregate[showcase]);
        }
        assert(expression_aggregate[showcase] ==
            GOLDEN_EXPRESSIONS[showcase]);
        assert(mouth_aggregate[showcase] ==
            GOLDEN_MOUTHS[showcase]);
    }

    /* Rendering another atlas between identical snapshots cannot leak state. */
    const face_render_key_t replay_key = expression_key(9U);
    const uint32_t before = render_checked(
        &players[0], &replay_key, 27000U, frame);
    (void)render_checked(
        &players[1], &replay_key, 27000U, frame);
    const uint32_t after = render_checked(
        &players[0], &replay_key, 27000U, frame);
    assert(before == after);

    printf(
        "expression hashes: %08x %08x\n",
        expression_aggregate[0],
        expression_aggregate[1]);
    printf(
        "mouth hashes:      %08x %08x\n",
        mouth_aggregate[0],
        mouth_aggregate[1]);
    for (size_t showcase = 0U; showcase < SHOWCASE_COUNT; ++showcase) {
        const face_sprite_showcase_info_t *info =
            face_sprite_showcase_info(showcase);
        printf(
            "%s: blob=%u portable_payload=%u cells=%u palette=%u\n",
            info->display_name,
            (unsigned)info->encoded_pixel_bytes,
            (unsigned)info->portable_payload_bytes,
            (unsigned)info->cell_count,
            (unsigned)info->palette_colors);
    }

    if (argc > 1) {
        char expression_path[1024];
        char mouth_path[1024];
        const int expression_length = snprintf(
            expression_path,
            sizeof(expression_path),
            "%s/face_sprite_showcase_expressions.ppm",
            argv[1]);
        const int mouth_length = snprintf(
            mouth_path,
            sizeof(mouth_path),
            "%s/face_sprite_showcase_mouths.ppm",
            argv[1]);
        assert(expression_length > 0);
        assert((size_t)expression_length < sizeof(expression_path));
        assert(mouth_length > 0);
        assert((size_t)mouth_length < sizeof(mouth_path));
        assert(write_expression_contact(expression_path, players));
        assert(write_mouth_contact(mouth_path, players));
        printf("wrote %s\n", expression_path);
        printf("wrote %s\n", mouth_path);
    }
    free(frame);
    return 0;
}
