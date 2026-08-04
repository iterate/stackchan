#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_pixel_pack.h"
#include "face_stage.h"

enum {
    MAX_COLUMNS = FACE_EXPRESSION_COUNT,
    SHEET_ROWS = FACE_PIXEL_PACK_PROFILE_COUNT,
    LABEL_WIDTH = 104,
    LABEL_HEIGHT = 12,
    CELL_GAP = 2,
    MAX_SHEET_WIDTH =
        LABEL_WIDTH +
        MAX_COLUMNS * (FACE_PIXEL_PACK_WIDTH + CELL_GAP),
    MAX_SHEET_HEIGHT =
        LABEL_HEIGHT +
        SHEET_ROWS * (FACE_PIXEL_PACK_HEIGHT + CELL_GAP),
};

static uint16_t frames[
    SHEET_ROWS][MAX_COLUMNS][FACE_PIXEL_PACK_PIXEL_COUNT];
static uint8_t sheet[
    MAX_SHEET_WIDTH * MAX_SHEET_HEIGHT * 3U];

static const char *const PROFILE_LABELS[SHEET_ROWS] = {
    "EGA QUEST",
    "VGA ELDER",
    "TALKIE",
    "ROGUE 1BIT",
};

static const char *const EXPRESSION_LABELS[FACE_EXPRESSION_COUNT] = {
    "NEUTRAL",
    "WARM",
    "JOY",
    "CONCERN",
    "SURPRISE",
    "THOUGHT",
    "SKEPTIC",
    "DETERMINED",
    "SLEEPY",
    "EXCITED",
    "EMBARRASSED",
};

static const char *const MOUTH_LABELS[10] = {
    "REST", "MBP", "FV", "SS", "EE",
    "EH", "AA", "OO", "OH", "SMALL",
};

static const uint8_t GLYPH_ROWS[][6] = {
    {'A', 2, 5, 7, 5, 5}, {'B', 6, 5, 6, 5, 6},
    {'C', 3, 4, 4, 4, 3}, {'D', 6, 5, 5, 5, 6},
    {'E', 7, 4, 6, 4, 7}, {'F', 7, 4, 6, 4, 4},
    {'G', 3, 4, 5, 5, 3}, {'H', 5, 5, 7, 5, 5},
    {'I', 7, 2, 2, 2, 7}, {'J', 1, 1, 1, 5, 2},
    {'K', 5, 5, 6, 5, 5}, {'L', 4, 4, 4, 4, 7},
    {'M', 7, 7, 5, 5, 5}, {'N', 5, 7, 7, 5, 5},
    {'O', 2, 5, 5, 5, 2}, {'P', 6, 5, 6, 4, 4},
    {'Q', 2, 5, 5, 2, 1}, {'R', 6, 5, 6, 5, 5},
    {'S', 3, 4, 2, 1, 6}, {'T', 7, 2, 2, 2, 2},
    {'U', 5, 5, 5, 5, 7}, {'V', 5, 5, 5, 2, 2},
    {'W', 5, 5, 5, 7, 7}, {'X', 5, 5, 2, 5, 5},
    {'Y', 5, 5, 2, 2, 2}, {'Z', 7, 1, 2, 4, 7},
    {'0', 2, 5, 5, 5, 2}, {'1', 2, 6, 2, 2, 7},
    {'2', 6, 1, 2, 4, 7}, {'3', 7, 1, 2, 1, 6},
    {'4', 5, 5, 7, 1, 1}, {'5', 7, 4, 6, 1, 6},
    {'6', 3, 4, 6, 5, 2}, {'7', 7, 1, 2, 2, 2},
    {'8', 2, 5, 2, 5, 2}, {'9', 2, 5, 3, 1, 6},
};

static uint8_t glyph_row(char character, int row)
{
    for (size_t index = 0U;
         index < sizeof(GLYPH_ROWS) / sizeof(GLYPH_ROWS[0]);
         ++index) {
        if (GLYPH_ROWS[index][0] == (uint8_t)character) {
            return GLYPH_ROWS[index][row + 1];
        }
    }
    return 0U;
}

static void put_rgb(
    int width,
    int height,
    int x,
    int y,
    uint8_t red,
    uint8_t green,
    uint8_t blue)
{
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }
    uint8_t *pixel =
        &sheet[((size_t)y * (size_t)width + (size_t)x) * 3U];
    pixel[0] = red;
    pixel[1] = green;
    pixel[2] = blue;
}

static void draw_text(
    int width,
    int height,
    int x,
    int y,
    const char *text,
    unsigned scale)
{
    for (size_t character = 0U;
         text[character] != '\0';
         ++character) {
        for (int row = 0; row < 5; ++row) {
            const uint8_t bits = glyph_row(text[character], row);
            for (int column = 0; column < 3; ++column) {
                if ((bits & (uint8_t)(4U >> column)) == 0U) {
                    continue;
                }
                for (unsigned offset_y = 0U;
                     offset_y < scale;
                     ++offset_y) {
                    for (unsigned offset_x = 0U;
                         offset_x < scale;
                         ++offset_x) {
                        put_rgb(
                            width, height,
                            x + (int)(column * scale + offset_x),
                            y + (int)(row * scale + offset_y),
                            232U, 238U, 248U);
                    }
                }
            }
        }
        x += (int)(4U * scale);
    }
}

static void copy_frame(
    int sheet_width,
    int sheet_height,
    int origin_x,
    int origin_y,
    const uint16_t *frame)
{
    for (int y = 0; y < FACE_PIXEL_PACK_HEIGHT; ++y) {
        for (int x = 0; x < FACE_PIXEL_PACK_WIDTH; ++x) {
            const uint16_t colour =
                frame[(size_t)y * FACE_PIXEL_PACK_WIDTH + (size_t)x];
            const uint8_t red5 =
                (uint8_t)((colour >> 11U) & 0x1fU);
            const uint8_t green6 =
                (uint8_t)((colour >> 5U) & 0x3fU);
            const uint8_t blue5 =
                (uint8_t)(colour & 0x1fU);
            put_rgb(
                sheet_width, sheet_height,
                origin_x + x, origin_y + y,
                (uint8_t)((red5 << 3U) | (red5 >> 2U)),
                (uint8_t)((green6 << 2U) | (green6 >> 4U)),
                (uint8_t)((blue5 << 3U) | (blue5 >> 2U)));
        }
    }
}

static bool write_sheet(
    const char *path,
    size_t columns,
    const char *const *column_labels)
{
    const int width =
        LABEL_WIDTH +
        (int)columns * (FACE_PIXEL_PACK_WIDTH + CELL_GAP);
    const int height =
        LABEL_HEIGHT +
        SHEET_ROWS * (FACE_PIXEL_PACK_HEIGHT + CELL_GAP);
    memset(sheet, 14, (size_t)width * (size_t)height * 3U);
    for (size_t column = 0U; column < columns; ++column) {
        const int cell_x =
            LABEL_WIDTH +
            (int)column * (FACE_PIXEL_PACK_WIDTH + CELL_GAP);
        draw_text(
            width, height, cell_x + 3, 3,
            column_labels[column], 1U);
    }
    for (size_t row = 0U; row < SHEET_ROWS; ++row) {
        const int cell_y =
            LABEL_HEIGHT +
            (int)row * (FACE_PIXEL_PACK_HEIGHT + CELL_GAP);
        draw_text(
            width, height, 4, cell_y + 53,
            PROFILE_LABELS[row], 1U);
        for (size_t column = 0U; column < columns; ++column) {
            const int cell_x =
                LABEL_WIDTH +
                (int)column *
                    (FACE_PIXEL_PACK_WIDTH + CELL_GAP);
            copy_frame(
                width, height, cell_x, cell_y,
                frames[row][column]);
        }
    }
    FILE *output = fopen(path, "wb");
    if (output == NULL) {
        return false;
    }
    fprintf(output, "P6\n%d %d\n255\n", width, height);
    const size_t bytes =
        (size_t)width * (size_t)height * 3U;
    const bool ok = fwrite(sheet, 1U, bytes, output) == bytes;
    return fclose(output) == 0 && ok;
}

static face_render_key_t base_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 126U;
    key.controls.mouth_width = 156U;
    key.controls.mouth_round = 44U;
    key.controls.mouth_teeth = 82U;
    key.controls.eye_left_open = 242U;
    key.controls.eye_right_open = 242U;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme = FACE_VISEME_AA;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_weight = 220U;
    key.viseme_blend = 34U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.affect_arousal = 128U;
    key.attention = 240U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

static void set_mouth_pose(face_render_key_t *key, size_t shape)
{
    key->viseme_weight = 0U;
    key->viseme_secondary = FACE_VISEME_NONE;
    key->controls.mouth_open = 0U;
    key->controls.mouth_width = 150U;
    key->controls.mouth_round = 0U;
    key->controls.mouth_press = 0U;
    key->controls.mouth_teeth = 0U;
    switch (shape) {
    case 1:
        key->controls.mouth_press = 235U;
        break;
    case 2:
        key->controls.mouth_open = 62U;
        key->controls.mouth_press = 130U;
        key->controls.mouth_teeth = 235U;
        break;
    case 3:
        key->controls.mouth_open = 10U;
        key->controls.mouth_teeth = 230U;
        break;
    case 4:
        key->controls.mouth_open = 72U;
        key->controls.mouth_width = 236U;
        key->controls.mouth_teeth = 180U;
        break;
    case 5:
        key->controls.mouth_open = 132U;
        key->controls.mouth_teeth = 120U;
        break;
    case 6:
        key->controls.mouth_open = 244U;
        key->controls.mouth_width = 168U;
        break;
    case 7:
        key->controls.mouth_open = 82U;
        key->controls.mouth_width = 82U;
        key->controls.mouth_round = 236U;
        break;
    case 8:
        key->controls.mouth_open = 206U;
        key->controls.mouth_width = 92U;
        key->controls.mouth_round = 232U;
        break;
    case 9:
        key->controls.mouth_open = 54U;
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    const char *prefix =
        argc > 1 ? argv[1] : "/tmp/face-pixel-pack";
    char expression_path[1024];
    char mouth_path[1024];
    assert(snprintf(
               expression_path, sizeof(expression_path),
               "%s__4-styles__11-stage-expressions__mid-speech.ppm",
               prefix) > 0);
    assert(snprintf(
               mouth_path, sizeof(mouth_path),
               "%s__4-styles__10-pcm-mouth-shapes__warm.ppm",
               prefix) > 0);

    const uint32_t fixed_clock =
        FACE_PIXEL_PACK_SAMPLE_RATE * 7U + 211U;
    for (size_t profile = 0U; profile < SHEET_ROWS; ++profile) {
        for (size_t expression = 0U;
             expression < FACE_EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = base_key();
            key.stage_expression = (uint8_t)expression;
            key.expression_weight = 255U;
            assert(face_pixel_pack_render(
                (face_pixel_pack_profile_t)profile,
                &key, fixed_clock,
                frames[profile][expression],
                FACE_PIXEL_PACK_PIXEL_COUNT));
        }
    }
    assert(write_sheet(
        expression_path, FACE_EXPRESSION_COUNT,
        EXPRESSION_LABELS));

    for (size_t profile = 0U; profile < SHEET_ROWS; ++profile) {
        for (size_t shape = 0U; shape < 10U; ++shape) {
            face_render_key_t key = base_key();
            key.stage_expression = FACE_EXPRESSION_WARM;
            key.expression_weight = 255U;
            set_mouth_pose(&key, shape);
            assert(face_pixel_pack_render(
                (face_pixel_pack_profile_t)profile,
                &key, fixed_clock,
                frames[profile][shape],
                FACE_PIXEL_PACK_PIXEL_COUNT));
        }
    }
    assert(write_sheet(mouth_path, 10U, MOUTH_LABELS));
    printf("%s\n%s\n", expression_path, mouth_path);
    return 0;
}
