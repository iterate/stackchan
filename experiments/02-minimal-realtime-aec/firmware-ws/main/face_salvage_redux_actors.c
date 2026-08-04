#include "face_salvage_redux_actors.h"

#include "face_pose.h"
#include "face_stage.h"

#include <string.h>

#define SR_RGB565(red, green, blue)                                      \
    ((uint16_t)((((uint16_t)(red) & 0xf8U) << 8U) |                     \
                (((uint16_t)(green) & 0xfcU) << 3U) |                   \
                ((uint16_t)(blue) >> 3U)))

enum {
    SR_SAFE = 4,
    SR_EXPRESSION_COUNT = 11,
};

typedef struct {
    const char *slug;
    const char *name;
    uint8_t legacy_id;
    uint8_t grammar;
    bool mouthless;
    bool pixel_hybrid;
    uint8_t ops;
    int8_t eye_x[2];
    int8_t eye_y;
    uint8_t eye_w;
    uint8_t eye_h;
    int8_t mouth_y;
    uint8_t mouth_max_w;
    uint8_t mouth_max_h;
} sr_actor_def_t;

typedef struct {
    int8_t eye_open_left;
    int8_t eye_open_right;
    int8_t eye_width;
    int8_t brow_raise_left;
    int8_t brow_raise_right;
    int8_t brow_slope_left;
    int8_t brow_slope_right;
    int8_t gaze_x;
    int8_t gaze_y;
    int8_t mouth_open;
    int8_t mouth_width;
    int8_t mouth_corner_left;
    int8_t mouth_corner_right;
    int8_t mouth_asymmetry;
    int8_t pupil;
    uint8_t cheek;
} sr_expression_t;

typedef struct {
    uint8_t open;
    uint8_t width;
    uint8_t round;
    uint8_t press;
    uint8_t teeth;
    uint8_t tongue;
    uint8_t consonant;
} sr_viseme_t;

typedef struct {
    uint16_t *pixels;
} sr_canvas_t;

static const sr_actor_def_t SR_ACTORS[FACE_SALVAGE_REDUX_COUNT] = {
    [FACE_SALVAGE_REDUX_STORY_SCOUT] = {
        "story-scout-redux",
        "Story Scout · cinematic shape actor",
        4U,
        FACE_SALVAGE_REDUX_GRAMMAR_SOFT_CINEMATIC,
        false,
        false,
        13U,
        {57, 103},
        51,
        32U,
        36U,
        88,
        64U,
        28U,
    },
    [FACE_SALVAGE_REDUX_POCKET_COURIER] = {
        "pocket-courier-redux",
        "Pocket Courier · handheld hybrid",
        6U,
        FACE_SALVAGE_REDUX_GRAMMAR_HANDHELD_HYBRID,
        false,
        true,
        7U,
        {58, 102},
        50,
        27U,
        28U,
        84,
        56U,
        24U,
    },
    [FACE_SALVAGE_REDUX_VELA_EYES] = {
        "vela-eyes-redux",
        "Vela · luminous eye-only actor",
        29U,
        FACE_SALVAGE_REDUX_GRAMMAR_LUMINOUS_EYES_ONLY,
        true,
        false,
        8U,
        {54, 106},
        60,
        36U,
        29U,
        0,
        0U,
        0U,
    },
    [FACE_SALVAGE_REDUX_KITE_ORACLE] = {
        "kite-oracle-redux",
        "Kite Oracle · folded-paper mask",
        35U,
        FACE_SALVAGE_REDUX_GRAMMAR_FOLDED_RIBBON,
        false,
        false,
        10U,
        {57, 103},
        51,
        29U,
        31U,
        84,
        66U,
        28U,
    },
    [FACE_SALVAGE_REDUX_ORBIT_GARDENER] = {
        "orbit-gardener-redux",
        "Orbit Gardener · petal automaton",
        36U,
        FACE_SALVAGE_REDUX_GRAMMAR_SEGMENTED_ORBIT,
        false,
        false,
        11U,
        {58, 102},
        51,
        27U,
        27U,
        85,
        58U,
        24U,
    },
    [FACE_SALVAGE_REDUX_FELT_FAMILIAR] = {
        "felt-familiar-redux",
        "Felt Familiar · theatre puppet",
        52U,
        FACE_SALVAGE_REDUX_GRAMMAR_STITCHED_ELASTIC,
        false,
        false,
        10U,
        {56, 104},
        52,
        31U,
        34U,
        88,
        62U,
        27U,
    },
};

/*
 * Authored expression deltas.  Every row changes topology: lid aperture,
 * brow pressure, gaze, mouth curvature/asymmetry, or pupil scale.
 */
static const sr_expression_t SR_EXPRESSIONS[SR_EXPRESSION_COUNT] = {
    /* neutral */
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0U},
    /* warm */
    {-2, -2, 3, -1, -1, -2, 2, 0, 1, 2, 5, 6, 6, 0, 0, 92U},
    /* joy */
    {-10, -10, 7, -2, -2, -4, 4, 0, 1, 6, 12, 12, 12, 0, 1, 226U},
    /* concern */
    {-2, -1, -2, -5, -5, 6, -6, -3, 3, -1, -7, -9, -8, -1, 1, 48U},
    /* surprise */
    {9, 9, 3, -8, -8, 0, 0, 0, -4, 13, -8, 0, 0, 0, -3, 0U},
    /* thoughtful */
    {-2, -8, 0, -5, 1, 5, -2, -9, -5, -2, -3, -3, 2, 5, 1, 24U},
    /* skeptical */
    {-9, -1, -1, -7, 2, 7, -6, 9, 0, -1, -4, -7, 4, 8, 0, 18U},
    /* determined */
    {-6, -6, 4, 3, 3, -7, 7, 0, 1, -2, 4, -6, -6, 0, 1, 12U},
    /* sleepy */
    {-15, -15, -5, 4, 4, 1, -1, -2, 5, -1, -1, 1, 1, 1, 2, 0U},
    /* excited */
    {7, 7, 8, -8, -8, -2, 2, 0, -4, 11, 12, 11, 11, -1, -2, 190U},
    /* embarrassed */
    {-6, -9, -3, -4, -6, 4, -4, 8, 5, -1, 2, 6, 0, 8, 1, 255U},
};

static const sr_viseme_t SR_VISEMES[FACE_VISEME_COUNT] = {
    [FACE_VISEME_AA] = {230U, 170U, 20U, 0U, 72U, 62U, 1U},
    [FACE_VISEME_E] = {92U, 246U, 4U, 0U, 184U, 6U, 2U},
    [FACE_VISEME_I] = {54U, 226U, 2U, 0U, 126U, 4U, 3U},
    [FACE_VISEME_O] = {204U, 92U, 246U, 0U, 24U, 38U, 4U},
    [FACE_VISEME_U] = {118U, 68U, 255U, 0U, 12U, 30U, 5U},
    [FACE_VISEME_PP] = {4U, 164U, 18U, 255U, 0U, 0U, 6U},
    [FACE_VISEME_SS] = {44U, 236U, 3U, 28U, 248U, 0U, 7U},
    [FACE_VISEME_TH] = {80U, 188U, 20U, 0U, 112U, 255U, 8U},
    [FACE_VISEME_DD] = {86U, 178U, 12U, 0U, 198U, 82U, 9U},
    [FACE_VISEME_FF] = {36U, 202U, 6U, 62U, 255U, 0U, 10U},
    [FACE_VISEME_KK] = {138U, 184U, 28U, 0U, 44U, 88U, 11U},
    [FACE_VISEME_NN] = {50U, 170U, 16U, 18U, 108U, 58U, 12U},
    [FACE_VISEME_RR] = {108U, 140U, 132U, 0U, 42U, 38U, 13U},
    [FACE_VISEME_CH] = {90U, 208U, 24U, 0U, 152U, 26U, 14U},
    [FACE_VISEME_SIL] = {4U, 142U, 22U, 230U, 0U, 0U, 15U},
};

static int sr_clamp(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int sr_abs(int value)
{
    return value < 0 ? -value : value;
}

static int sr_mix(int from, int to, uint8_t weight)
{
    return from + ((to - from) * (int)weight + 127) / 255;
}

static int sr_wave(uint32_t sample_clock, uint32_t period)
{
    if (period < 4U) {
        return 0;
    }
    const uint32_t phase = sample_clock % period;
    const uint32_t half = period / 2U;
    const uint32_t rising = phase < half ? phase : period - phase;
    return (int)(rising * 254U / half) - 127;
}

static sr_viseme_t sr_blended_viseme(const face_render_key_t *key)
{
    const uint8_t primary_index =
        key->viseme < FACE_VISEME_COUNT ? key->viseme : FACE_VISEME_SIL;
    const uint8_t secondary_index =
        key->viseme_secondary < FACE_VISEME_COUNT
        ? key->viseme_secondary
        : primary_index;
    const sr_viseme_t primary = SR_VISEMES[primary_index];
    const sr_viseme_t secondary = SR_VISEMES[secondary_index];
    sr_viseme_t result;
#define SR_BLEND(field)                                                   \
    result.field = (uint8_t)sr_mix(                                      \
        primary.field, secondary.field, key->viseme_blend)
    SR_BLEND(open);
    SR_BLEND(width);
    SR_BLEND(round);
    SR_BLEND(press);
    SR_BLEND(teeth);
    SR_BLEND(tongue);
    SR_BLEND(consonant);
#undef SR_BLEND
    return result;
}

static uint8_t sr_ir_phase(const face_render_key_t *key)
{
    const uint8_t *bytes = (const uint8_t *)key;
    uint16_t phase = 0U;
    for (size_t index = 0U; index < FACE_RENDER_KEY_BYTES; ++index) {
        phase = (uint16_t)(
            phase + (uint16_t)bytes[index] *
                (uint16_t)(index * 2U + 1U));
    }
    return (uint8_t)phase;
}

size_t face_salvage_redux_count(void)
{
    return FACE_SALVAGE_REDUX_COUNT;
}

const char *face_salvage_redux_slug(face_salvage_redux_style_t style)
{
    if ((unsigned)style >= FACE_SALVAGE_REDUX_COUNT) {
        return NULL;
    }
    return SR_ACTORS[style].slug;
}

const char *face_salvage_redux_name(face_salvage_redux_style_t style)
{
    if ((unsigned)style >= FACE_SALVAGE_REDUX_COUNT) {
        return NULL;
    }
    return SR_ACTORS[style].name;
}

bool face_salvage_redux_info(
    face_salvage_redux_style_t style,
    face_salvage_redux_info_t *info)
{
    if ((unsigned)style >= FACE_SALVAGE_REDUX_COUNT || info == NULL) {
        return false;
    }
    const sr_actor_def_t *actor = &SR_ACTORS[style];
    info->slug = actor->slug;
    info->name = actor->name;
    info->legacy_profile_id = actor->legacy_id;
    info->grammar = actor->grammar;
    info->mouthless = actor->mouthless;
    info->pixel_hybrid = actor->pixel_hybrid;
    info->estimated_ops_per_pixel = actor->ops;
    return true;
}

bool face_salvage_redux_from_legacy_id(
    uint8_t legacy_profile_id,
    face_salvage_redux_style_t *style)
{
    if (style == NULL) {
        return false;
    }
    for (size_t raw = 0U; raw < FACE_SALVAGE_REDUX_COUNT; ++raw) {
        if (SR_ACTORS[raw].legacy_id == legacy_profile_id) {
            *style = (face_salvage_redux_style_t)raw;
            return true;
        }
    }
    return false;
}

bool face_salvage_redux_resolve(
    face_salvage_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    face_salvage_redux_pose_t *pose)
{
    if ((unsigned)style >= FACE_SALVAGE_REDUX_COUNT ||
        render_key == NULL || pose == NULL) {
        return false;
    }
    memset(pose, 0, sizeof(*pose));
    memcpy(&pose->source, render_key, FACE_RENDER_KEY_BYTES);

    const sr_actor_def_t *actor = &SR_ACTORS[style];
    const uint8_t expression =
        render_key->stage_expression < SR_EXPRESSION_COUNT
        ? render_key->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
    const uint8_t expression_weight = render_key->expression_weight;
    const sr_expression_t *authored = &SR_EXPRESSIONS[expression];
#define SR_EXPR(field) sr_mix(0, authored->field, expression_weight)

    const sr_viseme_t viseme = sr_blended_viseme(render_key);
    pose->speaking =
        (render_key->controls.flags & FACE_KEYFRAME_FLAG_SPEAKING) != 0U ||
        render_key->controls.expression == FACE_ACTIVITY_SPEAKING ||
        render_key->speech_phase != FACE_SPEECH_IDLE;
    pose->speech_open = (uint8_t)sr_mix(
        render_key->controls.mouth_open,
        viseme.open,
        render_key->viseme_weight);
    pose->speech_width = (uint8_t)sr_mix(
        render_key->controls.mouth_width,
        viseme.width,
        render_key->viseme_weight);
    pose->speech_round = (uint8_t)sr_mix(
        render_key->controls.mouth_round,
        viseme.round,
        render_key->viseme_weight);
    pose->speech_press = (uint8_t)sr_mix(
        render_key->controls.mouth_press,
        viseme.press,
        render_key->viseme_weight);
    pose->teeth = (uint8_t)sr_mix(
        render_key->controls.mouth_teeth,
        viseme.teeth,
        render_key->viseme_weight);
    pose->tongue = (uint8_t)sr_mix(
        render_key->tongue,
        viseme.tongue,
        render_key->viseme_weight);
    pose->cheek = (uint8_t)sr_clamp(
        (int)render_key->cheek + SR_EXPR(cheek), 0, 255);
    pose->consonant = render_key->phoneme == FACE_PHONEME_NONE
        ? viseme.consonant
        : (uint8_t)(render_key->phoneme & 31U);

    int phase_gain;
    switch (render_key->speech_phase) {
    case FACE_SPEECH_STARTING:
        phase_gain = 104;
        break;
    case FACE_SPEECH_ACTIVE:
        phase_gain = 255;
        break;
    case FACE_SPEECH_ENDING:
        phase_gain = 92;
        break;
    case FACE_SPEECH_IDLE:
    default:
        phase_gain = pose->speaking ? 132 : 0;
        break;
    }
    const int speech_wave = sr_wave(
        sample_clock +
            (uint32_t)style * 137U +
            (uint32_t)render_key->schema_version * 17U,
        9600U);
    pose->speech_wave = pose->speaking
        ? (int16_t)sr_clamp(
            speech_wave *
                (24 + (int)render_key->audio_level * phase_gain / 255) /
                (127 * 24),
            -4,
            4)
        : 0;

    int gaze_x = sr_clamp(
        render_key->controls.look_x / 7 +
            render_key->head_yaw / 22 +
            SR_EXPR(gaze_x),
        -11,
        11);
    int gaze_y = sr_clamp(
        render_key->controls.look_y / 8 +
            render_key->head_pitch / 24 +
            SR_EXPR(gaze_y),
        -9,
        9);
    if (actor->pixel_hybrid) {
        gaze_x = gaze_x / 2 * 2;
        gaze_y = gaze_y / 2 * 2;
    }

    for (size_t eye = 0U; eye < 2U; ++eye) {
        const uint8_t input_open = eye == 0U
            ? render_key->controls.eye_left_open
            : render_key->controls.eye_right_open;
        const uint8_t squint = eye == 0U
            ? render_key->eye_left_squint
            : render_key->eye_right_squint;
        const int expression_open = eye == 0U
            ? SR_EXPR(eye_open_left)
            : SR_EXPR(eye_open_right);
        pose->eye_x[eye] = actor->eye_x[eye];
        pose->eye_y[eye] = actor->eye_y;
        pose->eye_w[eye] = (int16_t)sr_clamp(
            actor->eye_w + SR_EXPR(eye_width) +
                (actor->mouthless
                    ? ((int)pose->speech_width - 128) / 20
                    : 0),
            18,
            actor->mouthless ? 40 : 46);
        pose->eye_h[eye] = actor->eye_h;
        int open =
            actor->eye_h * (int)input_open / 255 +
            expression_open -
            (int)squint / 17 +
            ((int)render_key->affect_arousal - 128) / 50;
        if (actor->mouthless && pose->speaking) {
            open += ((int)pose->speech_open -
                     (int)pose->speech_press / 2 - 64) /
                38;
            open += (eye == 0U ? -1 : 1) *
                ((int)pose->speech_round - 128) / 72;
            open += pose->speech_wave;
        }
        if (render_key->speech_phase == FACE_SPEECH_STARTING) {
            open += actor->mouthless ? 5 : 3;
        } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
            open -= actor->mouthless ? 2 : 1;
        }
        if ((render_key->controls.flags &
                FACE_KEYFRAME_FLAG_BLINKING) != 0U) {
            open = 2;
        }
        pose->eye_open[eye] = (int16_t)sr_clamp(
            open, 2, actor->eye_h);
        pose->pupil_x[eye] =
            (int16_t)(actor->eye_x[eye] + gaze_x);
        pose->pupil_y[eye] =
            (int16_t)(actor->eye_y + gaze_y);
        pose->pupil_radius[eye] = (int16_t)sr_clamp(
            6 + SR_EXPR(pupil) +
                ((int)render_key->attention - 128) / 56 -
                ((int)render_key->affect_arousal - 128) / 88 +
                (actor->mouthless
                    ? ((int)pose->speech_round - 128) / 64
                    : 0),
            2,
            actor->mouthless ? 11 : 10);
        const int authored_raise = eye == 0U
            ? SR_EXPR(brow_raise_left)
            : SR_EXPR(brow_raise_right);
        const int outer = eye == 0U
            ? render_key->brow_outer_left
            : render_key->brow_outer_right;
        pose->brow_y[eye] = (int16_t)sr_clamp(
            actor->eye_y - actor->eye_h / 2 - 7 +
                authored_raise -
                render_key->controls.brow / 22 -
                render_key->brow_inner / 24 -
                outer / 30,
            10,
            49);
        pose->brow_slope[eye] = (int16_t)sr_clamp(
            (eye == 0U
                ? SR_EXPR(brow_slope_left)
                : SR_EXPR(brow_slope_right)) +
                render_key->head_roll / 18 +
                outer / 24,
            -13,
            13);
    }

    pose->mouth_x = 80;
    pose->mouth_y = actor->mouth_y;
    pose->mouth_w = actor->mouthless
        ? 0
        : (int16_t)sr_clamp(
            20 + (int)pose->speech_width * 42 / 255 -
                (int)pose->speech_round / 20 +
                SR_EXPR(mouth_width),
            14,
            actor->mouth_max_w);
    int mouth_height = actor->mouthless
        ? 0
        : 2 + (int)pose->speech_open * 27 / 255 -
            (int)pose->speech_press * 7 / 255 +
            SR_EXPR(mouth_open) + pose->speech_wave;
    if (render_key->speech_phase == FACE_SPEECH_STARTING) {
        mouth_height = 2 + (mouth_height - 2) * 2 / 5;
    } else if (render_key->speech_phase == FACE_SPEECH_ENDING) {
        mouth_height = 2 + (mouth_height - 2) * 2 / 5;
    }
    pose->mouth_h = actor->mouthless
        ? 0
        : (int16_t)sr_clamp(mouth_height, 2, actor->mouth_max_h);
    pose->mouth_corner[0] = (int16_t)sr_clamp(
        render_key->mouth_corner_left / 11 +
            render_key->affect_valence / 18 +
            SR_EXPR(mouth_corner_left),
        -14,
        14);
    pose->mouth_corner[1] = (int16_t)sr_clamp(
        render_key->mouth_corner_right / 11 +
            render_key->affect_valence / 18 +
            SR_EXPR(mouth_corner_right),
        -14,
        14);
    pose->mouth_asymmetry = (int16_t)sr_clamp(
        SR_EXPR(mouth_asymmetry) +
            ((int)render_key->mouth_corner_right -
             render_key->mouth_corner_left) /
                18 +
            render_key->head_roll / 26,
        -12,
        12);
    pose->body_lean_x = (int16_t)sr_clamp(
        render_key->body_lean_x / 15, -8, 8);
    pose->body_lean_y = (int16_t)sr_clamp(
        render_key->body_lean_y / 18, -6, 6);
    pose->detail_phase = sr_ir_phase(render_key);
    pose->stage_expression = expression;
    pose->expression_weight = expression_weight;
    pose->activity = render_key->controls.expression;
    pose->speech_phase = render_key->speech_phase;
    pose->attention = render_key->attention;
    pose->mouthless = actor->mouthless;
#undef SR_EXPR
    return true;
}

static void sr_fill(sr_canvas_t *canvas, uint16_t color)
{
    for (size_t index = 0U;
         index < FACE_SALVAGE_REDUX_PIXEL_COUNT;
         ++index) {
        canvas->pixels[index] = color;
    }
}

static void sr_put(sr_canvas_t *canvas, int x, int y, uint16_t color)
{
    if (x < SR_SAFE || x >= FACE_SALVAGE_REDUX_WIDTH - SR_SAFE ||
        y < SR_SAFE || y >= FACE_SALVAGE_REDUX_HEIGHT - SR_SAFE) {
        return;
    }
    canvas->pixels[
        (size_t)y * FACE_SALVAGE_REDUX_WIDTH + (size_t)x] = color;
}

static void sr_hline(
    sr_canvas_t *canvas,
    int x,
    int y,
    int width,
    uint16_t color)
{
    if (width <= 0 || y < SR_SAFE ||
        y >= FACE_SALVAGE_REDUX_HEIGHT - SR_SAFE) {
        return;
    }
    int start = sr_clamp(x, SR_SAFE, FACE_SALVAGE_REDUX_WIDTH - SR_SAFE);
    int end = sr_clamp(
        x + width, SR_SAFE, FACE_SALVAGE_REDUX_WIDTH - SR_SAFE);
    for (int px = start; px < end; ++px) {
        canvas->pixels[
            (size_t)y * FACE_SALVAGE_REDUX_WIDTH + (size_t)px] = color;
    }
}

static void sr_rect(
    sr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    const int start_y = sr_clamp(
        y, SR_SAFE, FACE_SALVAGE_REDUX_HEIGHT - SR_SAFE);
    const int end_y = sr_clamp(
        y + height, SR_SAFE, FACE_SALVAGE_REDUX_HEIGHT - SR_SAFE);
    for (int py = start_y; py < end_y; ++py) {
        sr_hline(canvas, x, py, width, color);
    }
}

static void sr_line(
    sr_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    uint16_t color)
{
    int dx = sr_abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -sr_abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        sr_put(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void sr_thick_line(
    sr_canvas_t *canvas,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    const int radius = thickness / 2;
    if (sr_abs(x1 - x0) >= sr_abs(y1 - y0)) {
        for (int offset = -radius; offset <= radius; ++offset) {
            sr_line(canvas, x0, y0 + offset, x1, y1 + offset, color);
        }
    } else {
        for (int offset = -radius; offset <= radius; ++offset) {
            sr_line(canvas, x0 + offset, y0, x1 + offset, y1, color);
        }
    }
}

static void sr_ellipse(
    sr_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    uint16_t color)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    const int rx2 = rx * rx;
    const int ry2 = ry * ry;
    const int limit = rx2 * ry2;
    for (int dy = -ry; dy <= ry; ++dy) {
        int half = rx;
        while (half > 0 &&
               half * half * ry2 + dy * dy * rx2 > limit) {
            --half;
        }
        sr_hline(canvas, cx - half, cy + dy, half * 2 + 1, color);
    }
}

static void sr_ellipse_ring(
    sr_canvas_t *canvas,
    int cx,
    int cy,
    int rx,
    int ry,
    int thickness,
    uint16_t outer,
    uint16_t inner)
{
    sr_ellipse(canvas, cx, cy, rx, ry, outer);
    sr_ellipse(
        canvas,
        cx,
        cy,
        sr_clamp(rx - thickness, 1, rx),
        sr_clamp(ry - thickness, 1, ry),
        inner);
}

static void sr_round_rect(
    sr_canvas_t *canvas,
    int x,
    int y,
    int width,
    int height,
    int radius,
    uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    radius = sr_clamp(radius, 0, sr_clamp(width / 2, 0, height / 2));
    if (radius == 0) {
        sr_rect(canvas, x, y, width, height, color);
        return;
    }
    sr_rect(canvas, x + radius, y, width - radius * 2, height, color);
    sr_rect(canvas, x, y + radius, radius, height - radius * 2, color);
    sr_rect(
        canvas,
        x + width - radius,
        y + radius,
        radius,
        height - radius * 2,
        color);
    sr_ellipse(canvas, x + radius, y + radius, radius, radius, color);
    sr_ellipse(
        canvas,
        x + width - radius - 1,
        y + radius,
        radius,
        radius,
        color);
    sr_ellipse(
        canvas,
        x + radius,
        y + height - radius - 1,
        radius,
        radius,
        color);
    sr_ellipse(
        canvas,
        x + width - radius - 1,
        y + height - radius - 1,
        radius,
        radius,
        color);
}

static int sr_edge(
    int ax,
    int ay,
    int bx,
    int by,
    int px,
    int py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void sr_triangle(
    sr_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    uint16_t color)
{
    const int min_x = sr_clamp(
        ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx),
        SR_SAFE,
        FACE_SALVAGE_REDUX_WIDTH - SR_SAFE - 1);
    const int max_x = sr_clamp(
        ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx),
        SR_SAFE,
        FACE_SALVAGE_REDUX_WIDTH - SR_SAFE - 1);
    const int min_y = sr_clamp(
        ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy),
        SR_SAFE,
        FACE_SALVAGE_REDUX_HEIGHT - SR_SAFE - 1);
    const int max_y = sr_clamp(
        ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy),
        SR_SAFE,
        FACE_SALVAGE_REDUX_HEIGHT - SR_SAFE - 1);
    const int orientation = sr_edge(ax, ay, bx, by, cx, cy);
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const int e0 = sr_edge(ax, ay, bx, by, x, y);
            const int e1 = sr_edge(bx, by, cx, cy, x, y);
            const int e2 = sr_edge(cx, cy, ax, ay, x, y);
            if ((orientation >= 0 &&
                    e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                (orientation < 0 &&
                    e0 <= 0 && e1 <= 0 && e2 <= 0)) {
                sr_put(canvas, x, y, color);
            }
        }
    }
}

static void sr_quad(
    sr_canvas_t *canvas,
    int ax,
    int ay,
    int bx,
    int by,
    int cx,
    int cy,
    int dx,
    int dy,
    uint16_t color)
{
    sr_triangle(canvas, ax, ay, bx, by, cx, cy, color);
    sr_triangle(canvas, ax, ay, cx, cy, dx, dy, color);
}

static void sr_curve(
    sr_canvas_t *canvas,
    int x0,
    int y0,
    int cx,
    int cy,
    int x1,
    int y1,
    int thickness,
    uint16_t color)
{
    int previous_x = x0;
    int previous_y = y0;
    for (int step = 1; step <= 12; ++step) {
        const int inverse = 12 - step;
        const int x =
            (inverse * inverse * x0 +
             2 * inverse * step * cx +
             step * step * x1) /
            144;
        const int y =
            (inverse * inverse * y0 +
             2 * inverse * step * cy +
             step * step * y1) /
            144;
        sr_thick_line(
            canvas,
            previous_x,
            previous_y,
            x,
            y,
            thickness,
            color);
        previous_x = x;
        previous_y = y;
    }
}

static void sr_trim_bits(
    sr_canvas_t *canvas,
    int x,
    int y,
    int step_x,
    int step_y,
    uint8_t bits,
    uint16_t bright,
    uint16_t dim)
{
    for (int bit = 0; bit < 8; ++bit) {
        const uint16_t color =
            (bits & (uint8_t)(1U << bit)) != 0U ? bright : dim;
        const int px = x + step_x * bit;
        const int py = y + step_y * bit;
        sr_rect(canvas, px, py, 3, 2, color);
    }
}

static void sr_soft_eye(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    size_t eye,
    uint16_t socket,
    uint16_t sclera,
    uint16_t iris,
    uint16_t pupil,
    uint16_t lid,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int rx = pose->eye_w[eye] / 2;
    const int open = sr_clamp(
        pose->eye_open[eye], 2, pose->eye_h[eye]);
    const int ry = sr_clamp(open / 2, 1, pose->eye_h[eye] / 2);
    sr_ellipse(canvas, cx, cy, rx + 3, ry + 3, socket);
    sr_ellipse(canvas, cx, cy, rx, ry, sclera);

    const int max_gaze_x = sr_clamp(rx - 7, 1, 9);
    const int max_gaze_y = sr_clamp(ry - 6, 0, 7);
    const int px = cx + sr_clamp(
        pose->pupil_x[eye] - cx, -max_gaze_x, max_gaze_x);
    const int py = cy + sr_clamp(
        pose->pupil_y[eye] - cy, -max_gaze_y, max_gaze_y);
    const int iris_r = sr_clamp(pose->pupil_radius[eye] + 3, 5, 12);
    const int pupil_r = sr_clamp(pose->pupil_radius[eye], 2, iris_r - 2);
    if (ry >= 4) {
        sr_ellipse(canvas, px, py, iris_r, sr_clamp(iris_r, 2, ry), iris);
        sr_ellipse(canvas, px, py, pupil_r, sr_clamp(pupil_r, 1, ry), pupil);
        sr_ellipse(canvas, px - 2, py - 2, 2, 2, glint);
        if ((pose->detail_phase & (uint8_t)(1U << eye)) != 0U) {
            sr_put(canvas, px + 3, py + 2, glint);
        }
    }

    const int slope = pose->brow_slope[eye];
    const int top = cy - ry;
    sr_thick_line(
        canvas,
        cx - rx,
        top + slope / 2,
        cx + rx,
        top - slope / 2,
        2,
        lid);
}

static void sr_brow(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    size_t eye,
    int half_width,
    int thickness,
    uint16_t color)
{
    const int cx = pose->eye_x[eye];
    const int y = pose->brow_y[eye];
    const int slope = pose->brow_slope[eye];
    sr_curve(
        canvas,
        cx - half_width,
        y + slope / 2,
        cx,
        y - 2,
        cx + half_width,
        y - slope / 2,
        thickness,
        color);
}

static void sr_story_mouth(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    uint16_t skin,
    uint16_t lip,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x;
    const int cy = pose->mouth_y;
    const int half = pose->mouth_w / 2;
    const int left_y = cy - pose->mouth_corner[0] / 2;
    const int right_y = cy - pose->mouth_corner[1] / 2 +
        pose->mouth_asymmetry / 3;
    if (pose->mouth_h <= 4 || pose->speech_press > 196U ||
        pose->stage_expression == FACE_EXPRESSION_DETERMINED) {
        const int center_y =
            cy + (pose->mouth_corner[0] + pose->mouth_corner[1]) / 3 +
            (pose->stage_expression == FACE_EXPRESSION_CONCERN ? 5 : 0);
        sr_curve(
            canvas,
            cx - half,
            left_y,
            cx,
            center_y,
            cx + half,
            right_y,
            pose->stage_expression == FACE_EXPRESSION_DETERMINED ? 3 : 2,
            lip);
        if (pose->stage_expression == FACE_EXPRESSION_DETERMINED) {
            sr_curve(
                canvas,
                cx - half + 4,
                left_y + 3,
                cx,
                center_y + 2,
                cx + half - 4,
                right_y + 3,
                1,
                cavity);
        }
        return;
    }

    const int height = pose->mouth_h;
    const int half2 = half * half;
    for (int dx = -half; dx <= half; ++dx) {
        const int arch =
            height * (half2 - dx * dx) / (half2 == 0 ? 1 : half2);
        const int edge_y =
            left_y + (right_y - left_y) * (dx + half) /
                (half * 2 == 0 ? 1 : half * 2);
        const int top = edge_y - arch * 2 / 5;
        const int bottom = edge_y + arch * 3 / 5;
        sr_hline(canvas, cx + dx, top, 1, cavity);
        for (int y = top + 1; y <= bottom; ++y) {
            sr_put(canvas, cx + dx, y, cavity);
        }
    }
    sr_curve(
        canvas,
        cx - half,
        left_y,
        cx,
        cy - height * 2 / 5 - 1,
        cx + half,
        right_y,
        2,
        lip);
    sr_curve(
        canvas,
        cx - half,
        left_y,
        cx,
        cy + height * 3 / 5 + 1,
        cx + half,
        right_y,
        2,
        lip);

    if (pose->teeth > 92U && height > 8) {
        const int tooth_width = sr_clamp(
            pose->mouth_w * (int)pose->teeth / 320, 8, pose->mouth_w - 8);
        sr_round_rect(
            canvas,
            cx - tooth_width / 2,
            cy - height / 3,
            tooth_width,
            sr_clamp(height / 4, 2, 5),
            1,
            teeth);
    }
    if (pose->tongue > 74U && height > 11) {
        const int tongue_width = sr_clamp(
            pose->mouth_w * (int)pose->tongue / 430, 7, pose->mouth_w - 10);
        sr_ellipse(
            canvas,
            cx + pose->mouth_asymmetry / 3,
            cy + height / 3,
            tongue_width / 2,
            sr_clamp(height / 5, 2, 5),
            tongue);
    }
    (void)skin;
}

/*
 * Topology changes sit on top of the continuously interpolated pose.  Keep
 * them dormant during a partial stage cue: the pose still eases eyes, brows,
 * gaze, and mouth corners, while the categorical end pose appears only when
 * an animator explicitly asks for a near-full expression.  This prevents a
 * one-frame socket/ear/crest swap at cue attack and release boundaries.
 */
static uint8_t sr_discrete_expression(
    const face_salvage_redux_pose_t *pose)
{
    return pose->expression_weight >= 240U
        ? pose->stage_expression
        : FACE_EXPRESSION_NEUTRAL;
}

static void sr_pocket_eye(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    size_t eye,
    uint16_t outline,
    uint16_t white,
    uint16_t iris,
    uint16_t pupil)
{
    const int cx = pose->eye_x[eye] / 2 * 2;
    const int cy = pose->eye_y[eye] / 2 * 2;
    const int width = sr_clamp(pose->eye_w[eye] / 2 * 2, 20, 36);
    const int open = sr_clamp(pose->eye_open[eye] / 2 * 2, 2, 28);
    const uint8_t expression = sr_discrete_expression(pose);

    /*
     * Closed-eye performances are authored as chunky curves rather than
     * tiny pupils inside nearly closed rectangular sockets.  The curves
     * retain their expression after the exact 4:1 device downsample.
     */
    if (expression == FACE_EXPRESSION_JOY ||
        expression == FACE_EXPRESSION_SLEEPY) {
        const int center_y = expression == FACE_EXPRESSION_JOY
            ? cy - 3
            : cy + 2;
        const int edge_y = expression == FACE_EXPRESSION_JOY
            ? cy + 3
            : cy;
        sr_thick_line(
            canvas,
            cx - width / 2,
            edge_y,
            cx,
            center_y,
            4,
            outline);
        sr_thick_line(
            canvas,
            cx,
            center_y,
            cx + width / 2,
            edge_y,
            4,
            outline);
        const int slope = pose->brow_slope[eye] / 2 * 2;
        sr_thick_line(
            canvas,
            cx - width / 2,
            pose->brow_y[eye] + slope / 2,
            cx + width / 2,
            pose->brow_y[eye] - slope / 2,
            3,
            outline);
        return;
    }

    sr_rect(
        canvas,
        cx - width / 2 - 2,
        cy - open / 2 - 2,
        width + 4,
        open + 4,
        outline);
    sr_rect(
        canvas,
        cx - width / 2,
        cy - open / 2,
        width,
        open,
        white);
    if (open >= 6) {
        const int px = cx + sr_clamp(
            pose->pupil_x[eye] - pose->eye_x[eye], -8, 8);
        const int py = cy + sr_clamp(
            pose->pupil_y[eye] - pose->eye_y[eye], -5, 5);
        sr_rect(canvas, px - 4, py - 5, 8, 10, iris);
        sr_rect(canvas, px - 2, py - 3, 4, 6, pupil);
        sr_rect(canvas, px - 2, py - 3, 2, 2, white);
    }

    if (expression == FACE_EXPRESSION_DETERMINED) {
        const int inner = eye == 0U ? cx + width / 2 : cx - width / 2;
        const int outer = eye == 0U ? cx - width / 2 : cx + width / 2;
        sr_thick_line(
            canvas,
            outer,
            cy - open / 2,
            inner,
            cy - open / 2 + 7,
            4,
            outline);
    } else if (
        expression == FACE_EXPRESSION_THOUGHTFUL &&
        eye == 1U) {
        sr_rect(
            canvas,
            cx - width / 2,
            cy - open / 2,
            width,
            sr_clamp(open / 3, 3, 7),
            outline);
    } else if (
        expression == FACE_EXPRESSION_SKEPTICAL &&
        eye == 0U) {
        sr_thick_line(
            canvas,
            cx - width / 2,
            cy - open / 2 + 5,
            cx + width / 2,
            cy - open / 2,
            4,
            outline);
    }

    const int slope = pose->brow_slope[eye] / 2 * 2;
    sr_thick_line(
        canvas,
        cx - width / 2,
        pose->brow_y[eye] + slope / 2,
        cx + width / 2,
        pose->brow_y[eye] - slope / 2,
        2,
        outline);
}

static uint8_t sr_viseme_family(
    const face_salvage_redux_pose_t *pose)
{
    if (pose->consonant == 0U) {
        return 15U;
    }
    return (uint8_t)(((pose->consonant - 1U) % 15U) + 1U);
}

static void sr_pocket_expression_mouth(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x / 2 * 2;
    const int cy = pose->mouth_y / 2 * 2;
    switch (sr_discrete_expression(pose)) {
    case FACE_EXPRESSION_WARM:
        sr_thick_line(canvas, cx - 16, cy - 3, cx, cy + 5, 5, outline);
        sr_thick_line(canvas, cx, cy + 5, cx + 16, cy - 3, 5, outline);
        break;
    case FACE_EXPRESSION_JOY:
        sr_rect(canvas, cx - 18, cy - 3, 36, 4, outline);
        sr_round_rect(canvas, cx - 14, cy + 1, 28, 7, 3, cavity);
        sr_rect(canvas, cx - 9, cy + 1, 18, 3, teeth);
        break;
    case FACE_EXPRESSION_CONCERN:
        sr_thick_line(canvas, cx - 14, cy + 5, cx, cy - 3, 3, outline);
        sr_thick_line(canvas, cx, cy - 3, cx + 14, cy + 5, 3, outline);
        break;
    case FACE_EXPRESSION_SURPRISE:
        sr_ellipse_ring(canvas, cx, cy + 1, 7, 8, 3, outline, cavity);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        sr_thick_line(canvas, cx - 13, cy + 1, cx + 6, cy - 2, 3, outline);
        sr_rect(canvas, cx + 8, cy - 2, 4, 4, outline);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        sr_thick_line(canvas, cx - 15, cy + 4, cx + 14, cy - 3, 4, outline);
        sr_rect(canvas, cx + 10, cy - 5, 5, 3, cavity);
        break;
    case FACE_EXPRESSION_DETERMINED:
        sr_rect(canvas, cx - 18, cy - 2, 36, 5, outline);
        sr_rect(canvas, cx - 12, cy + 4, 24, 3, outline);
        break;
    case FACE_EXPRESSION_SLEEPY:
        sr_rect(canvas, cx - 8, cy, 16, 3, outline);
        break;
    case FACE_EXPRESSION_EXCITED:
        sr_round_rect(canvas, cx - 15, cy - 6, 30, 14, 4, outline);
        sr_round_rect(canvas, cx - 11, cy - 3, 22, 8, 3, cavity);
        sr_rect(canvas, cx - 7, cy + 2, 14, 3, tongue);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        sr_thick_line(canvas, cx - 13, cy - 3, cx - 2, cy + 3, 3, outline);
        sr_thick_line(canvas, cx - 2, cy + 3, cx + 14, cy, 3, outline);
        break;
    case FACE_EXPRESSION_NEUTRAL:
    default:
        sr_rect(canvas, cx - 10, cy, 20, 3, outline);
        break;
    }
}

static void sr_pocket_mouth(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    uint16_t outline,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x / 2 * 2;
    const int cy = pose->mouth_y / 2 * 2;
    const uint8_t family = sr_viseme_family(pose);
    if (!pose->speaking ||
        pose->speech_phase == FACE_SPEECH_IDLE ||
        family == 15U) {
        sr_pocket_expression_mouth(
            canvas, pose, outline, cavity, teeth, tongue);
        return;
    }

    const int width = sr_clamp(pose->mouth_w / 4 * 4, 16, 44);
    const int height = sr_clamp(pose->mouth_h / 2 * 2, 2, 20);
    switch (family) {
    case 1: { /* AA — compact vertical jaw, never a beard-sized block. */
        const int w = sr_clamp(width - 12, 18, 26);
        const int h = sr_clamp(height, 8, 15);
        sr_round_rect(canvas, cx - w / 2 - 2, cy - h / 2 - 2, w + 4, h + 4, 4, outline);
        sr_round_rect(canvas, cx - w / 2, cy - h / 2, w, h, 3, cavity);
        if (pose->tongue > 48U) {
            sr_rect(canvas, cx - 7, cy + h / 2 - 4, 14, 4, tongue);
        }
        break;
    }
    case 2: { /* E */
        const int w = sr_clamp(width, 32, 44);
        sr_rect(canvas, cx - w / 2, cy - 5, w, 11, outline);
        sr_rect(canvas, cx - w / 2 + 4, cy - 2, w - 8, 5, teeth);
        for (int x = cx - w / 2 + 8; x < cx + w / 2 - 4; x += 8) {
            sr_rect(canvas, x, cy - 2, 2, 5, cavity);
        }
        break;
    }
    case 3: /* I */
        sr_rect(canvas, cx - 18, cy - 2, 36, 5, outline);
        sr_rect(canvas, cx - 12, cy - 1, 24, 2, teeth);
        break;
    case 4: { /* O */
        const int radius = sr_clamp(height / 2, 6, 9);
        sr_ellipse_ring(canvas, cx, cy, radius, radius + 1, 3, outline, cavity);
        break;
    }
    case 5: /* U */
        sr_round_rect(canvas, cx - 8, cy - 5, 16, 12, 5, outline);
        sr_round_rect(canvas, cx - 4, cy - 2, 8, 6, 3, cavity);
        sr_rect(canvas, cx - 12, cy + 5, 24, 3, outline);
        break;
    case 6: /* PP */
        sr_rect(canvas, cx - 15, cy - 3, 30, 3, outline);
        sr_rect(canvas, cx - 12, cy + 2, 24, 3, outline);
        break;
    case 7: { /* SS */
        const int w = sr_clamp(width, 32, 44);
        sr_rect(canvas, cx - w / 2, cy - 4, w, 9, outline);
        sr_rect(canvas, cx - w / 2 + 3, cy - 2, w - 6, 5, teeth);
        sr_rect(canvas, cx - w / 2 + 8, cy - 2, 3, 5, cavity);
        sr_rect(canvas, cx + w / 2 - 11, cy - 2, 3, 5, cavity);
        break;
    }
    case 8: /* TH */
        sr_round_rect(canvas, cx - 17, cy - 5, 34, 12, 3, outline);
        sr_rect(canvas, cx - 13, cy - 3, 26, 4, teeth);
        sr_rect(canvas, cx - 8, cy + 2, 16, 4, tongue);
        break;
    case 9: /* DD */
        sr_round_rect(canvas, cx - 14, cy - 5, 28, 11, 3, outline);
        sr_rect(canvas, cx - 10, cy - 3, 20, 4, teeth);
        sr_rect(canvas, cx - 5, cy + 2, 10, 3, cavity);
        break;
    case 10: /* FF */
        sr_rect(canvas, cx - 17, cy - 5, 34, 9, outline);
        sr_rect(canvas, cx - 13, cy - 3, 26, 4, teeth);
        sr_thick_line(canvas, cx - 11, cy + 5, cx + 11, cy + 2, 3, tongue);
        break;
    case 11: /* KK */
        sr_round_rect(canvas, cx - 15, cy - 7, 30, 15, 3, outline);
        sr_rect(canvas, cx - 11, cy - 4, 22, 8, cavity);
        sr_rect(canvas, cx + 5, cy - 4, 4, 8, tongue);
        break;
    case 12: /* NN */
        sr_rect(canvas, cx - 16, cy - 3, 32, 7, outline);
        sr_rect(canvas, cx - 10, cy, 20, 4, tongue);
        sr_rect(canvas, cx - 4, cy - 3, 8, 3, teeth);
        break;
    case 13: /* RR */
        sr_round_rect(canvas, cx - 11, cy - 6, 22, 13, 5, outline);
        sr_round_rect(canvas, cx - 6, cy - 3, 12, 7, 3, cavity);
        sr_rect(canvas, cx - 4, cy + 2, 8, 3, tongue);
        break;
    case 14: /* CH */
        sr_round_rect(canvas, cx - 13, cy - 4, 26, 9, 4, outline);
        sr_rect(canvas, cx - 8, cy - 2, 16, 5, cavity);
        sr_thick_line(canvas, cx - 18, cy - 1, cx - 14, cy + 3, 3, outline);
        sr_thick_line(canvas, cx + 14, cy + 3, cx + 18, cy - 1, 3, outline);
        break;
    case 15:
    default:
        sr_pocket_expression_mouth(
            canvas, pose, outline, cavity, teeth, tongue);
        break;
    }
}

static void sr_vela_eye(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    size_t eye,
    uint16_t glow,
    uint16_t core,
    uint16_t plate)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int width = pose->eye_w[eye];
    const int open = pose->eye_open[eye];
    const int half = width / 2;
    const int ry = sr_clamp(open / 2, 1, 17);
    sr_ellipse(canvas, cx, cy, half + 3, ry + 3, glow);
    sr_ellipse(canvas, cx, cy, half, ry, core);

    /*
     * The voice is performed entirely in the eyes.  Consonant families cut
     * authored inner/outer notches and vowel roundness changes the aperture.
     */
    const int family = pose->consonant % 15U;
    const int notch = 2 + family % 5;
    if (family == 5 || family == 9 || family == 14) {
        sr_rect(canvas, cx - half, cy - 1, width, 3, plate);
    } else if (family == 6 || family == 7 || family == 13) {
        const int inner_x = eye == 0U ? cx + half - notch : cx - half;
        sr_triangle(
            canvas,
            inner_x,
            cy - ry,
            inner_x + (eye == 0U ? -notch : notch),
            cy,
            inner_x,
            cy + ry,
            plate);
    } else if (family == 3 || family == 4 || family == 12) {
        const int narrow = sr_clamp(
            half / 2 - (family == 4 ? 2 : 0), 4, half);
        sr_rect(
            canvas,
            cx - half - 1,
            cy - ry - 1,
            half - narrow + 1,
            ry * 2 + 3,
            plate);
        sr_rect(
            canvas,
            cx + narrow,
            cy - ry - 1,
            half - narrow + 2,
            ry * 2 + 3,
            plate);
        sr_ellipse(
            canvas,
            cx,
            cy,
            narrow,
            sr_clamp(ry - 1, 1, ry),
            core);
    }

    const int slope =
        pose->brow_slope[eye] +
        (family & 1 ? (eye == 0U ? 2 : -2) : 0);
    sr_thick_line(
        canvas,
        cx - half,
        cy - ry + slope / 2,
        cx + half,
        cy - ry - slope / 2,
        2,
        glow);
    if ((pose->detail_phase & (uint8_t)(1U << (eye + 2U))) != 0U &&
        open > 7) {
        sr_rect(
            canvas,
            cx - half / 2 + (family % 3) * 3,
            cy - ry / 2,
            3,
            2,
            SR_RGB565(235, 255, 255));
    }
}

static void sr_kite_eye(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    size_t eye,
    uint16_t ink,
    uint16_t paper,
    uint16_t iris,
    uint16_t glint)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int half_w = pose->eye_w[eye] / 2;
    const int half_h = sr_clamp(pose->eye_open[eye] / 2, 1, 16);
    const int slope = pose->brow_slope[eye];
    sr_quad(
        canvas,
        cx - half_w,
        cy + slope / 3,
        cx,
        cy - half_h,
        cx + half_w,
        cy - slope / 3,
        cx,
        cy + half_h,
        ink);
    sr_quad(
        canvas,
        cx - half_w + 3,
        cy + slope / 3,
        cx,
        cy - half_h + 3,
        cx + half_w - 3,
        cy - slope / 3,
        cx,
        cy + half_h - 3,
        paper);
    if (half_h > 4) {
        const int px = cx + sr_clamp(
            pose->pupil_x[eye] - cx, -6, 6);
        const int py = cy + sr_clamp(
            pose->pupil_y[eye] - cy, -4, 4);
        sr_ellipse(
            canvas,
            px,
            py,
            pose->pupil_radius[eye],
            sr_clamp(pose->pupil_radius[eye], 2, half_h - 1),
            iris);
        sr_put(canvas, px - 2, py - 2, glint);
    }
}

static void sr_kite_mouth(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    uint16_t ink,
    uint16_t fold,
    uint16_t inner,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x;
    const int cy = pose->mouth_y;
    const int half = pose->mouth_w / 2;
    const int left_y = cy - pose->mouth_corner[0] / 3;
    const int right_y = cy - pose->mouth_corner[1] / 3 +
        pose->mouth_asymmetry / 2;
    if (pose->mouth_h <= 4 || pose->speech_press > 192U) {
        sr_thick_line(
            canvas,
            cx - half,
            left_y,
            cx,
            cy + (pose->stage_expression == FACE_EXPRESSION_CONCERN ? 4 : 0),
            3,
            fold);
        sr_thick_line(
            canvas,
            cx,
            cy + (pose->stage_expression == FACE_EXPRESSION_CONCERN ? 4 : 0),
            cx + half,
            right_y,
            3,
            fold);
        if (pose->stage_expression == FACE_EXPRESSION_DETERMINED) {
            sr_line(
                canvas,
                cx - half + 5,
                cy + 4,
                cx + half - 5,
                cy + 4,
                ink);
        }
        return;
    }
    const int height = pose->mouth_h;
    sr_quad(
        canvas,
        cx - half,
        left_y,
        cx,
        cy - height / 2,
        cx + half,
        right_y,
        cx,
        cy + height / 2,
        ink);
    sr_quad(
        canvas,
        cx - half + 3,
        left_y,
        cx,
        cy - height / 2 + 3,
        cx + half - 3,
        right_y,
        cx + pose->mouth_asymmetry / 2,
        cy + height / 2 - 3,
        inner);
    sr_thick_line(
        canvas,
        cx - half + 1,
        left_y,
        cx,
        cy - height / 2,
        3,
        fold);
    sr_thick_line(
        canvas,
        cx,
        cy - height / 2,
        cx + half - 1,
        right_y,
        3,
        fold);
    sr_thick_line(
        canvas,
        cx - half + 2,
        left_y + 1,
        cx + pose->mouth_asymmetry / 2,
        cy + height / 2 - 1,
        2,
        fold);
    sr_thick_line(
        canvas,
        cx + pose->mouth_asymmetry / 2,
        cy + height / 2 - 1,
        cx + half - 2,
        right_y + 1,
        2,
        fold);
    if (pose->teeth > 108U && height > 9) {
        sr_triangle(
            canvas,
            cx - half + 6,
            left_y + 2,
            cx,
            cy - height / 2 + 4,
            cx + half - 6,
            right_y + 2,
            teeth);
    }
    if (pose->tongue > 96U && height > 11) {
        sr_triangle(
            canvas,
            cx - 7,
            cy + height / 5,
            cx + 8,
            cy + height / 5,
            cx + pose->mouth_asymmetry / 2,
            cy + height / 2 - 2,
            tongue);
    }
}

static void sr_orbit_eye(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    size_t eye,
    uint16_t rim,
    uint16_t white,
    uint16_t iris,
    uint16_t pupil)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int radius = pose->eye_w[eye] / 2;
    const int open = pose->eye_open[eye];
    sr_ellipse(canvas, cx, cy, radius + 3, radius + 3, rim);
    sr_ellipse(
        canvas,
        cx,
        cy,
        radius,
        sr_clamp(open / 2, 1, radius),
        white);
    if (open > 5) {
        const int px = cx + sr_clamp(
            pose->pupil_x[eye] - cx, -6, 6);
        const int py = cy + sr_clamp(
            pose->pupil_y[eye] - cy, -5, 5);
        const int petal = sr_clamp(pose->pupil_radius[eye], 3, 9);
        sr_ellipse(canvas, px - petal / 2, py, petal, 3, iris);
        sr_ellipse(canvas, px + petal / 2, py, petal, 3, iris);
        sr_ellipse(canvas, px, py - petal / 2, 3, petal, iris);
        sr_ellipse(canvas, px, py + petal / 2, 3, petal, iris);
        sr_ellipse(canvas, px, py, 3, 3, pupil);
    }
}

static void sr_orbit_mouth(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    uint16_t dark,
    uint16_t glow,
    uint16_t hot,
    uint16_t tongue)
{
    const int cx = pose->mouth_x;
    const int cy = pose->mouth_y;
    const int width = pose->mouth_w;
    const int height = pose->mouth_h;
    const int segments = 7;
    const int step = sr_clamp(width / segments, 4, 9);
    const int total = step * segments;
    if (height > 5 && pose->speech_press < 205U) {
        sr_round_rect(
            canvas,
            cx - total / 2 - 2,
            cy - height / 2 - 2,
            total + 4,
            height + 4,
            6,
            dark);
    }
    for (int segment = 0; segment < segments; ++segment) {
        const int distance = sr_abs(segment - segments / 2);
        const int curve =
            (pose->mouth_corner[0] + pose->mouth_corner[1]) *
                (3 - distance) /
                12;
        const int asym =
            pose->mouth_asymmetry * (segment - segments / 2) / 7;
        int bar_h = height <= 4
            ? 2
            : sr_clamp(
                3 + height * (4 - distance) / 5 +
                    ((int)pose->consonant + segment * 3) % 4,
                3,
                height);
        if (pose->stage_expression == FACE_EXPRESSION_DETERMINED) {
            bar_h = 2;
        }
        const int x = cx - total / 2 + segment * step + 1;
        const int y = cy - bar_h / 2 - curve + asym;
        sr_round_rect(
            canvas,
            x,
            y,
            sr_clamp(step - 2, 2, 6),
            bar_h,
            1,
            segment == 3 ? hot : glow);
    }
    if (pose->tongue > 120U && height > 13) {
        sr_rect(canvas, cx - 7, cy + height / 3, 14, 3, tongue);
    }
}

static void sr_felt_eye(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    size_t eye,
    uint16_t stitch,
    uint16_t white,
    uint16_t iris,
    uint16_t pupil)
{
    const int cx = pose->eye_x[eye];
    const int cy = pose->eye_y[eye];
    const int rx = pose->eye_w[eye] / 2;
    const int ry = sr_clamp(pose->eye_open[eye] / 2, 1, 17);
    const uint8_t expression = sr_discrete_expression(pose);

    if (expression == FACE_EXPRESSION_JOY ||
        expression == FACE_EXPRESSION_SLEEPY) {
        const bool joy =
            expression == FACE_EXPRESSION_JOY;
        const int center_y = joy ? cy - 3 : cy + 2;
        const int edge_y = joy ? cy + 4 : cy + 2;
        if (joy) {
            sr_thick_line(
                canvas,
                cx - rx,
                edge_y,
                cx,
                center_y,
                6,
                stitch);
            sr_thick_line(
                canvas,
                cx,
                center_y,
                cx + rx,
                edge_y,
                6,
                stitch);
            sr_thick_line(
                canvas,
                cx - rx + 2,
                edge_y - 1,
                cx,
                center_y + 1,
                4,
                white);
            sr_thick_line(
                canvas,
                cx,
                center_y + 1,
                cx + rx - 2,
                edge_y - 1,
                4,
                white);
        } else {
            sr_round_rect(
                canvas,
                cx - rx,
                cy,
                rx * 2,
                6,
                3,
                stitch);
            sr_rect(canvas, cx - rx + 2, cy + 1, rx * 2 - 4, 4, white);
        }
        sr_line(
            canvas,
            cx - rx - 2,
            edge_y - 2,
            cx - rx + 2,
            edge_y + 3,
            stitch);
        sr_line(
            canvas,
            cx + rx - 2,
            edge_y + 3,
            cx + rx + 2,
            edge_y - 2,
            stitch);
        return;
    }

    sr_ellipse(canvas, cx, cy, rx + 3, ry + 3, stitch);
    sr_ellipse(canvas, cx, cy, rx, ry, white);
    if (ry > 3) {
        const int px = cx + sr_clamp(
            pose->pupil_x[eye] - cx, -7, 7);
        const int py = cy + sr_clamp(
            pose->pupil_y[eye] - cy, -5, 5);
        sr_ellipse(canvas, px, py, 7, sr_clamp(7, 2, ry), iris);
        sr_ellipse(canvas, px, py, 3, sr_clamp(3, 1, ry), pupil);
        sr_put(canvas, px - 2, py - 2, white);
    }

    if (expression == FACE_EXPRESSION_DETERMINED) {
        const int inner_x = eye == 0U ? cx + rx : cx - rx;
        const int outer_x = eye == 0U ? cx - rx : cx + rx;
        sr_thick_line(
            canvas,
            outer_x,
            cy - ry,
            inner_x,
            cy - ry + 8,
            4,
            stitch);
    } else if (
        expression == FACE_EXPRESSION_THOUGHTFUL &&
        eye == 1U) {
        sr_ellipse(canvas, cx, cy - ry + 2, rx, 5, stitch);
    } else if (
        expression == FACE_EXPRESSION_SKEPTICAL &&
        eye == 0U) {
        sr_thick_line(
            canvas,
            cx - rx,
            cy - ry + 6,
            cx + rx,
            cy - ry,
            4,
            stitch);
    }

    for (int stitch_index = -1; stitch_index <= 1; ++stitch_index) {
        const int x = cx + stitch_index * rx;
        sr_line(
            canvas,
            x - 2,
            cy - ry - 3,
            x + 1,
            cy - ry,
            stitch);
    }
}

static void sr_felt_expression_mouth(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    uint16_t stitch,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x;
    const int cy = pose->mouth_y;
    switch (sr_discrete_expression(pose)) {
    case FACE_EXPRESSION_WARM:
        sr_curve(
            canvas,
            cx - 17,
            cy - 3,
            cx,
            cy + 7,
            cx + 17,
            cy - 3,
            4,
            stitch);
        sr_line(canvas, cx - 19, cy - 6, cx - 15, cy, stitch);
        sr_line(canvas, cx + 15, cy, cx + 19, cy - 6, stitch);
        break;
    case FACE_EXPRESSION_JOY:
        sr_round_rect(canvas, cx - 17, cy - 5, 34, 15, 6, stitch);
        sr_round_rect(canvas, cx - 13, cy - 2, 26, 9, 4, cavity);
        sr_rect(canvas, cx - 9, cy - 2, 18, 3, teeth);
        sr_ellipse(canvas, cx, cy + 5, 7, 3, tongue);
        break;
    case FACE_EXPRESSION_CONCERN:
        sr_curve(
            canvas,
            cx - 16,
            cy + 6,
            cx,
            cy - 5,
            cx + 16,
            cy + 6,
            4,
            stitch);
        break;
    case FACE_EXPRESSION_SURPRISE:
        sr_ellipse_ring(canvas, cx, cy + 1, 8, 9, 3, stitch, cavity);
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        sr_curve(
            canvas,
            cx - 15,
            cy + 2,
            cx - 2,
            cy - 2,
            cx + 10,
            cy,
            3,
            stitch);
        sr_line(canvas, cx + 13, cy - 3, cx + 16, cy + 1, stitch);
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        sr_curve(
            canvas,
            cx - 17,
            cy + 5,
            cx,
            cy,
            cx + 17,
            cy - 5,
            4,
            stitch);
        break;
    case FACE_EXPRESSION_DETERMINED:
        sr_thick_line(canvas, cx - 18, cy - 2, cx + 18, cy + 1, 4, stitch);
        sr_line(canvas, cx - 13, cy + 5, cx + 13, cy + 5, stitch);
        break;
    case FACE_EXPRESSION_SLEEPY:
        sr_curve(canvas, cx - 8, cy, cx, cy + 2, cx + 8, cy, 3, stitch);
        break;
    case FACE_EXPRESSION_EXCITED:
        sr_ellipse_ring(canvas, cx, cy + 1, 14, 9, 4, stitch, cavity);
        sr_ellipse(canvas, cx, cy + 6, 8, 3, tongue);
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        sr_curve(
            canvas,
            cx - 15,
            cy - 3,
            cx - 3,
            cy + 5,
            cx + 15,
            cy,
            4,
            stitch);
        break;
    case FACE_EXPRESSION_NEUTRAL:
    default:
        sr_curve(canvas, cx - 11, cy, cx, cy + 1, cx + 11, cy, 3, stitch);
        break;
    }
}

static void sr_felt_mouth(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose,
    uint16_t stitch,
    uint16_t cavity,
    uint16_t teeth,
    uint16_t tongue)
{
    const int cx = pose->mouth_x;
    const int cy = pose->mouth_y;
    const uint8_t family = sr_viseme_family(pose);
    if (!pose->speaking ||
        pose->speech_phase == FACE_SPEECH_IDLE ||
        family == 15U) {
        sr_felt_expression_mouth(
            canvas, pose, stitch, cavity, teeth, tongue);
        return;
    }

    const int width = sr_clamp(pose->mouth_w, 18, 42);
    const int height = sr_clamp(pose->mouth_h, 2, 18);
    switch (family) {
    case 1: { /* AA */
        const int w = sr_clamp(width - 8, 20, 30);
        const int h = sr_clamp(height, 9, 17);
        sr_ellipse_ring(
            canvas,
            cx,
            cy,
            w / 2 + 2,
            h / 2 + 2,
            3,
            stitch,
            cavity);
        if (pose->tongue > 48U) {
            sr_ellipse(canvas, cx, cy + h / 3, 7, 3, tongue);
        }
        break;
    }
    case 2: { /* E */
        const int w = sr_clamp(width, 32, 42);
        sr_round_rect(canvas, cx - w / 2, cy - 5, w, 11, 4, stitch);
        sr_round_rect(
            canvas,
            cx - w / 2 + 4,
            cy - 2,
            w - 8,
            5,
            2,
            teeth);
        sr_line(canvas, cx - 8, cy - 2, cx - 8, cy + 2, cavity);
        sr_line(canvas, cx + 8, cy - 2, cx + 8, cy + 2, cavity);
        break;
    }
    case 3: /* I */
        sr_curve(canvas, cx - 19, cy, cx, cy - 2, cx + 19, cy, 4, stitch);
        sr_curve(
            canvas,
            cx - 15,
            cy + 3,
            cx,
            cy + 5,
            cx + 15,
            cy + 3,
            2,
            teeth);
        break;
    case 4: /* O */
        sr_ellipse_ring(canvas, cx, cy, 8, 9, 3, stitch, cavity);
        break;
    case 5: /* U */
        sr_ellipse_ring(canvas, cx, cy, 9, 7, 3, stitch, cavity);
        sr_curve(
            canvas,
            cx - 12,
            cy + 5,
            cx,
            cy + 9,
            cx + 12,
            cy + 5,
            3,
            stitch);
        break;
    case 6: /* PP */
        sr_curve(
            canvas,
            cx - 15,
            cy - 2,
            cx,
            cy + 1,
            cx + 15,
            cy - 2,
            4,
            stitch);
        sr_curve(
            canvas,
            cx - 12,
            cy + 3,
            cx,
            cy + 5,
            cx + 12,
            cy + 3,
            2,
            stitch);
        break;
    case 7: { /* SS */
        const int w = sr_clamp(width, 32, 42);
        sr_round_rect(canvas, cx - w / 2, cy - 4, w, 9, 3, stitch);
        sr_rect(canvas, cx - w / 2 + 4, cy - 2, w - 8, 5, teeth);
        sr_line(canvas, cx - 9, cy - 2, cx - 9, cy + 2, cavity);
        sr_line(canvas, cx, cy - 2, cx, cy + 2, cavity);
        sr_line(canvas, cx + 9, cy - 2, cx + 9, cy + 2, cavity);
        break;
    }
    case 8: /* TH */
        sr_round_rect(canvas, cx - 17, cy - 5, 34, 12, 4, stitch);
        sr_rect(canvas, cx - 13, cy - 3, 26, 4, teeth);
        sr_ellipse(canvas, cx, cy + 4, 9, 3, tongue);
        break;
    case 9: /* DD */
        sr_round_rect(canvas, cx - 14, cy - 6, 28, 13, 5, stitch);
        sr_rect(canvas, cx - 10, cy - 3, 20, 4, teeth);
        sr_curve(
            canvas,
            cx - 7,
            cy + 3,
            cx,
            cy + 5,
            cx + 7,
            cy + 3,
            2,
            cavity);
        break;
    case 10: /* FF */
        sr_curve(
            canvas,
            cx - 17,
            cy - 4,
            cx,
            cy - 6,
            cx + 17,
            cy - 4,
            4,
            stitch);
        sr_rect(canvas, cx - 13, cy - 4, 26, 4, teeth);
        sr_curve(
            canvas,
            cx - 12,
            cy + 3,
            cx,
            cy,
            cx + 12,
            cy + 3,
            4,
            tongue);
        break;
    case 11: /* KK */
        sr_quad(
            canvas,
            cx - 15,
            cy - 3,
            cx - 8,
            cy - 8,
            cx + 15,
            cy - 5,
            cx + 10,
            cy + 8,
            stitch);
        sr_round_rect(canvas, cx - 9, cy - 4, 19, 9, 3, cavity);
        sr_ellipse(canvas, cx + 5, cy + 2, 4, 3, tongue);
        break;
    case 12: /* NN */
        sr_curve(
            canvas,
            cx - 16,
            cy - 2,
            cx,
            cy + 3,
            cx + 16,
            cy - 2,
            4,
            stitch);
        sr_ellipse(canvas, cx, cy + 3, 10, 3, tongue);
        sr_rect(canvas, cx - 5, cy - 3, 10, 3, teeth);
        break;
    case 13: /* RR */
        sr_ellipse_ring(canvas, cx, cy, 11, 7, 3, stitch, cavity);
        sr_curve(
            canvas,
            cx - 5,
            cy + 2,
            cx,
            cy + 5,
            cx + 5,
            cy + 2,
            3,
            tongue);
        break;
    case 14: /* CH */
        sr_round_rect(canvas, cx - 14, cy - 6, 28, 12, 5, stitch);
        sr_round_rect(canvas, cx - 9, cy - 3, 18, 7, 3, cavity);
        sr_line(canvas, cx - 19, cy - 5, cx - 15, cy + 3, stitch);
        sr_line(canvas, cx + 15, cy + 3, cx + 19, cy - 5, stitch);
        break;
    case 15:
    default:
        sr_felt_expression_mouth(
            canvas, pose, stitch, cavity, teeth, tongue);
        break;
    }
}

static void sr_draw_story_scout(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose)
{
    const uint16_t background = SR_RGB565(16, 27, 48);
    const uint16_t shadow = SR_RGB565(10, 16, 31);
    const uint16_t jacket = SR_RGB565(34, 92, 128);
    const uint16_t jacket_light = SR_RGB565(57, 143, 171);
    const uint16_t scarf = SR_RGB565(242, 126, 68);
    const uint16_t scarf_dark = SR_RGB565(157, 62, 48);
    const uint16_t hair = SR_RGB565(48, 35, 47);
    const uint16_t hair_light = SR_RGB565(91, 56, 59);
    const uint16_t skin = SR_RGB565(235, 173, 121);
    const uint16_t skin_light = SR_RGB565(255, 206, 151);
    const uint16_t cheek = SR_RGB565(234, 112, 103);
    const uint16_t eye_socket = SR_RGB565(57, 39, 48);
    const uint16_t white = SR_RGB565(255, 248, 225);
    const uint16_t iris = SR_RGB565(51, 139, 145);
    const uint16_t pupil = SR_RGB565(16, 35, 46);
    const uint16_t mouth = SR_RGB565(77, 28, 39);
    const uint16_t lip = SR_RGB565(128, 43, 53);
    const uint16_t tongue = SR_RGB565(218, 82, 94);

    sr_fill(canvas, background);
    sr_ellipse(canvas, 80, 114, 62, 10, shadow);

    /* The body leans independently.  Facial sockets never translate. */
    const int bx = pose->body_lean_x;
    const int by = pose->body_lean_y;
    sr_ellipse(canvas, 80 + bx, 119 + by, 64, 29, jacket);
    sr_triangle(
        canvas,
        25 + bx,
        116 + by,
        52 + bx,
        91 + by,
        70 + bx,
        116 + by,
        jacket_light);
    sr_triangle(
        canvas,
        135 + bx,
        116 + by,
        108 + bx,
        91 + by,
        90 + bx,
        116 + by,
        jacket_light);
    sr_round_rect(canvas, 62 + bx, 92 + by, 36, 27, 9, scarf_dark);
    sr_triangle(
        canvas,
        58 + bx,
        100 + by,
        80 + bx,
        114 + by,
        101 + bx,
        100 + by,
        scarf);

    sr_ellipse(canvas, 31, 61, 10, 15, hair);
    sr_ellipse(canvas, 129, 61, 10, 15, hair);
    sr_ellipse(canvas, 80, 58, 52, 53, hair);
    sr_ellipse(canvas, 80, 61, 47, 48, skin);
    sr_ellipse(canvas, 32, 62, 7, 11, skin);
    sr_ellipse(canvas, 128, 62, 7, 11, skin);
    sr_ellipse(canvas, 32, 62, 3, 6, skin_light);
    sr_ellipse(canvas, 128, 62, 3, 6, skin_light);

    /* Large/small curves and a single directional sweep give clean shape. */
    sr_ellipse(canvas, 61, 19, 34, 15, hair_light);
    sr_triangle(canvas, 35, 31, 51, 10, 73, 28, hair);
    sr_triangle(canvas, 56, 23, 93, 9, 119, 31, hair);
    sr_curve(canvas, 42, 31, 78, 11, 119, 31, 4, hair);

    sr_soft_eye(
        canvas,
        pose,
        0U,
        eye_socket,
        white,
        iris,
        pupil,
        hair,
        white);
    sr_soft_eye(
        canvas,
        pose,
        1U,
        eye_socket,
        white,
        iris,
        pupil,
        hair,
        white);
    sr_brow(canvas, pose, 0U, 15, 3, hair);
    sr_brow(canvas, pose, 1U, 15, 3, hair);

    sr_triangle(canvas, 78, 59, 75, 73, 84, 72, skin_light);
    sr_line(canvas, 76, 74, 82, 75, hair_light);
    if (pose->cheek > 54U) {
        const int cheek_width = 4 + (int)pose->cheek / 48;
        sr_ellipse(canvas, 42, 76, cheek_width, 3, cheek);
        sr_ellipse(canvas, 118, 76, cheek_width, 3, cheek);
    }

    sr_story_mouth(
        canvas,
        pose,
        skin,
        lip,
        mouth,
        white,
        tongue);

    /* Eight embroidered scarf beads are the transport-integrated detail. */
    sr_trim_bits(
        canvas,
        60 + bx,
        108 + by,
        5,
        0,
        pose->detail_phase,
        SR_RGB565(255, 220, 103),
        scarf_dark);
}

static void sr_draw_pocket_courier(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose)
{
    const uint16_t background = SR_RGB565(179, 197, 147);
    const uint16_t backdrop = SR_RGB565(104, 130, 98);
    const uint16_t outline = SR_RGB565(35, 48, 49);
    const uint16_t cloak = SR_RGB565(56, 82, 75);
    const uint16_t cloak_light = SR_RGB565(88, 119, 91);
    const uint16_t hood = SR_RGB565(72, 55, 67);
    const uint16_t hood_light = SR_RGB565(122, 73, 79);
    const uint16_t skin = SR_RGB565(224, 174, 112);
    const uint16_t skin_light = SR_RGB565(250, 213, 151);
    const uint16_t white = SR_RGB565(240, 238, 195);
    const uint16_t iris = SR_RGB565(40, 136, 121);
    const uint16_t mouth = SR_RGB565(68, 36, 44);
    const uint16_t teeth = SR_RGB565(255, 237, 180);
    const uint16_t tongue = SR_RGB565(198, 77, 76);
    const uint16_t gold = SR_RGB565(231, 176, 62);

    sr_fill(canvas, background);
    sr_rect(canvas, 4, 104, 152, 12, backdrop);
    sr_rect(canvas, 12, 108, 136, 8, outline);

    const int bx = pose->body_lean_x / 2 * 2;
    const int by = pose->body_lean_y / 2 * 2;
    sr_rect(canvas, 28 + bx, 94 + by, 104, 22, cloak);
    sr_rect(canvas, 20 + bx, 104 + by, 120, 12, cloak);
    sr_rect(canvas, 36 + bx, 92 + by, 12, 24, cloak_light);
    sr_rect(canvas, 112 + bx, 92 + by, 12, 24, cloak_light);

    /* Stepped handheld silhouette, drawn on a two-pixel logical grid. */
    sr_rect(canvas, 34, 18, 92, 82, outline);
    sr_rect(canvas, 26, 34, 108, 52, outline);
    sr_rect(canvas, 30, 28, 100, 64, hood);
    sr_rect(canvas, 38, 20, 84, 78, hood);
    sr_rect(canvas, 42, 32, 76, 62, skin);
    sr_rect(canvas, 38, 40, 84, 42, skin);
    sr_rect(canvas, 30, 46, 10, 24, skin);
    sr_rect(canvas, 120, 46, 10, 24, skin);
    sr_rect(canvas, 32, 50, 4, 14, skin_light);
    sr_rect(canvas, 124, 50, 4, 14, skin_light);

    sr_rect(canvas, 34, 22, 92, 10, hood_light);
    sr_rect(canvas, 48, 16, 64, 8, hood);
    int crest_left_y = 14;
    int crest_mid_y = 11;
    int crest_right_y = 14;
    switch (sr_discrete_expression(pose)) {
    case FACE_EXPRESSION_JOY:
    case FACE_EXPRESSION_EXCITED:
        crest_left_y = 15;
        crest_mid_y = 8;
        crest_right_y = 15;
        break;
    case FACE_EXPRESSION_CONCERN:
        crest_left_y = 10;
        crest_mid_y = 15;
        crest_right_y = 10;
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        crest_left_y = 10;
        crest_mid_y = 12;
        crest_right_y = 17;
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        crest_left_y = 17;
        crest_mid_y = 12;
        crest_right_y = 9;
        break;
    case FACE_EXPRESSION_SLEEPY:
        crest_left_y = 16;
        crest_mid_y = 16;
        crest_right_y = 16;
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        crest_left_y = 15;
        crest_mid_y = 13;
        crest_right_y = 17;
        break;
    default:
        break;
    }
    sr_thick_line(
        canvas,
        54,
        crest_left_y,
        80,
        crest_mid_y,
        5,
        gold);
    sr_thick_line(
        canvas,
        80,
        crest_mid_y,
        106,
        crest_right_y,
        5,
        gold);
    sr_rect(canvas, 77, crest_mid_y - 2, 6, 5, outline);
    sr_rect(canvas, 42, 32, 28, 8, hood);
    sr_rect(canvas, 90, 32, 28, 8, hood);

    sr_pocket_eye(canvas, pose, 0U, outline, white, iris, outline);
    sr_pocket_eye(canvas, pose, 1U, outline, white, iris, outline);
    sr_rect(canvas, 76, 61, 8, 8, skin_light);
    sr_rect(canvas, 80, 67, 6, 4, outline);

    if (pose->cheek > 56U) {
        const int blocks = 1 + pose->cheek / 80U;
        for (int block = 0; block < blocks; ++block) {
            sr_rect(canvas, 43 + block * 5, 72, 3, 2, hood_light);
            sr_rect(canvas, 114 - block * 5, 72, 3, 2, hood_light);
        }
    }
    sr_pocket_mouth(canvas, pose, outline, mouth, teeth, tongue);

    sr_rect(canvas, 51 + bx, 101 + by, 58, 12, outline);
    sr_rect(canvas, 55 + bx, 103 + by, 50, 8, cloak_light);
    sr_trim_bits(
        canvas,
        57 + bx,
        106 + by,
        6,
        0,
        pose->detail_phase,
        gold,
        cloak);
}

static void sr_draw_vela_eyes(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose)
{
    const uint16_t background = SR_RGB565(4, 8, 18);
    const uint16_t shadow = SR_RGB565(2, 4, 9);
    const uint16_t shell = SR_RGB565(22, 37, 57);
    const uint16_t shell_light = SR_RGB565(45, 69, 91);
    const uint16_t plate = SR_RGB565(7, 16, 30);
    const uint16_t glow = SR_RGB565(52, 223, 225);
    const uint16_t core = SR_RGB565(202, 255, 247);
    const uint16_t accent = SR_RGB565(112, 119, 255);
    const uint16_t dim = SR_RGB565(29, 77, 88);

    sr_fill(canvas, background);
    sr_ellipse(canvas, 80, 111, 59, 7, shadow);
    sr_round_rect(canvas, 10, 17, 140, 91, 31, shell);
    sr_round_rect(canvas, 15, 21, 130, 83, 27, shell_light);
    sr_round_rect(canvas, 19, 24, 122, 77, 24, plate);

    sr_rect(canvas, 7, 48, 8, 28, shell);
    sr_rect(canvas, 145, 48, 8, 28, shell);
    sr_rect(canvas, 10, 54, 5, 16, accent);
    sr_rect(canvas, 145, 54, 5, 16, accent);

    sr_vela_eye(canvas, pose, 0U, glow, core, plate);
    sr_vela_eye(canvas, pose, 1U, glow, core, plate);

    /* A lower bezel, not a mouth: eight stable vents carry protocol detail. */
    sr_curve(canvas, 49, 94, 80, 99, 111, 94, 2, shell_light);
    sr_trim_bits(
        canvas,
        58,
        96,
        6,
        0,
        pose->detail_phase,
        accent,
        dim);
}

static void sr_draw_kite_oracle(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose)
{
    const uint16_t background = SR_RGB565(36, 26, 57);
    const uint16_t shadow = SR_RGB565(20, 15, 35);
    const uint16_t cloak = SR_RGB565(51, 56, 92);
    const uint16_t cloak_light = SR_RGB565(91, 93, 139);
    const uint16_t ink = SR_RGB565(37, 34, 51);
    const uint16_t paper = SR_RGB565(223, 198, 162);
    const uint16_t paper_light = SR_RGB565(255, 231, 184);
    const uint16_t fold_blue = SR_RGB565(89, 138, 157);
    const uint16_t fold_red = SR_RGB565(196, 79, 87);
    const uint16_t iris = SR_RGB565(47, 109, 126);
    const uint16_t mouth = SR_RGB565(62, 32, 44);
    const uint16_t tongue = SR_RGB565(210, 92, 102);
    const uint16_t gold = SR_RGB565(235, 174, 74);

    sr_fill(canvas, background);
    sr_ellipse(canvas, 80, 114, 65, 9, shadow);
    const int bx = pose->body_lean_x;
    const int by = pose->body_lean_y;
    sr_triangle(
        canvas,
        17 + bx,
        116 + by,
        54 + bx,
        86 + by,
        83 + bx,
        116 + by,
        cloak);
    sr_triangle(
        canvas,
        143 + bx,
        116 + by,
        106 + bx,
        86 + by,
        77 + bx,
        116 + by,
        cloak_light);

    /* Layered asymmetric kite silhouette and coherent folded planes. */
    sr_quad(canvas, 80, 7, 137, 49, 80, 108, 23, 49, ink);
    sr_quad(canvas, 80, 12, 131, 50, 80, 102, 29, 50, paper);
    sr_triangle(canvas, 80, 12, 80, 102, 29, 50, paper_light);
    sr_triangle(canvas, 80, 12, 131, 50, 80, 56, fold_blue);
    sr_triangle(canvas, 29, 50, 80, 56, 80, 102, SR_RGB565(191, 158, 134));
    sr_triangle(canvas, 131, 50, 80, 56, 80, 102, SR_RGB565(169, 113, 116));
    sr_triangle(canvas, 23, 49, 9, 41, 26, 69, fold_red);
    sr_triangle(canvas, 137, 49, 151, 41, 134, 69, fold_blue);

    sr_kite_eye(
        canvas,
        pose,
        0U,
        ink,
        paper_light,
        iris,
        paper_light);
    sr_kite_eye(
        canvas,
        pose,
        1U,
        ink,
        paper_light,
        iris,
        paper_light);
    sr_brow(canvas, pose, 0U, 14, 2, ink);
    sr_brow(canvas, pose, 1U, 14, 2, ink);

    sr_triangle(canvas, 80, 57, 74, 73, 84, 71, fold_red);
    sr_line(canvas, 74, 73, 83, 75, ink);
    if (pose->cheek > 62U) {
        sr_triangle(canvas, 39, 73, 49, 68, 48, 78, fold_red);
        sr_triangle(canvas, 121, 73, 111, 68, 112, 78, fold_blue);
    }
    sr_kite_mouth(
        canvas,
        pose,
        ink,
        fold_red,
        mouth,
        paper_light,
        tongue);

    sr_triangle(
        canvas,
        49 + bx,
        104 + by,
        80 + bx,
        116 + by,
        111 + bx,
        104 + by,
        ink);
    sr_trim_bits(
        canvas,
        59 + bx,
        107 + by,
        6,
        0,
        pose->detail_phase,
        gold,
        cloak_light);
}

static void sr_draw_orbit_gardener(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose)
{
    const uint16_t background = SR_RGB565(11, 32, 35);
    const uint16_t shadow = SR_RGB565(5, 18, 20);
    const uint16_t body = SR_RGB565(37, 92, 78);
    const uint16_t body_light = SR_RGB565(73, 137, 99);
    const uint16_t rim = SR_RGB565(113, 170, 119);
    const uint16_t shell = SR_RGB565(58, 110, 91);
    const uint16_t face = SR_RGB565(20, 53, 56);
    const uint16_t cream = SR_RGB565(235, 225, 169);
    const uint16_t leaf = SR_RGB565(118, 200, 109);
    const uint16_t flower = SR_RGB565(242, 167, 82);
    const uint16_t pupil = SR_RGB565(30, 50, 47);
    const uint16_t mouth = SR_RGB565(8, 25, 30);
    const uint16_t glow = SR_RGB565(116, 226, 179);
    const uint16_t hot = SR_RGB565(255, 206, 101);
    const uint16_t tongue = SR_RGB565(225, 98, 92);

    sr_fill(canvas, background);
    sr_ellipse(canvas, 80, 114, 61, 8, shadow);
    const int bx = pose->body_lean_x;
    const int by = pose->body_lean_y;
    sr_ellipse(canvas, 80 + bx, 119 + by, 60, 27, body);
    sr_triangle(
        canvas,
        28 + bx,
        116 + by,
        55 + bx,
        91 + by,
        68 + bx,
        116 + by,
        body_light);
    sr_triangle(
        canvas,
        132 + bx,
        116 + by,
        105 + bx,
        91 + by,
        92 + bx,
        116 + by,
        body_light);

    sr_line(canvas, 80, 13, 80, 23, rim);
    sr_ellipse(canvas, 80, 11, 5, 5, flower);
    sr_ellipse(canvas, 75, 10, 4, 3, leaf);
    sr_ellipse(canvas, 85, 10, 4, 3, leaf);
    sr_ellipse(canvas, 25, 53, 12, 18, leaf);
    sr_ellipse(canvas, 135, 53, 12, 18, leaf);
    sr_ellipse_ring(canvas, 80, 57, 55, 52, 5, rim, shell);
    sr_ellipse(canvas, 80, 59, 44, 41, face);

    /* Broken orbital accents prevent the ring from becoming a flat circle. */
    sr_curve(canvas, 37, 36, 52, 18, 70, 15, 3, flower);
    sr_curve(canvas, 90, 15, 110, 19, 124, 37, 3, leaf);
    sr_rect(canvas, 29, 63, 5, 13, flower);
    sr_rect(canvas, 126, 63, 5, 13, leaf);

    sr_orbit_eye(canvas, pose, 0U, rim, cream, leaf, pupil);
    sr_orbit_eye(canvas, pose, 1U, rim, cream, leaf, pupil);
    sr_brow(canvas, pose, 0U, 13, 2, glow);
    sr_brow(canvas, pose, 1U, 13, 2, glow);
    sr_triangle(canvas, 80, 61, 75, 71, 84, 71, flower);
    sr_orbit_mouth(canvas, pose, mouth, glow, hot, tongue);

    sr_round_rect(
        canvas,
        53 + bx,
        102 + by,
        54,
        14,
        6,
        face);
    sr_trim_bits(
        canvas,
        58 + bx,
        107 + by,
        6,
        0,
        pose->detail_phase,
        hot,
        body);
}

static void sr_draw_felt_familiar(
    sr_canvas_t *canvas,
    const face_salvage_redux_pose_t *pose)
{
    const uint16_t background = SR_RGB565(72, 47, 54);
    const uint16_t shadow = SR_RGB565(43, 30, 39);
    const uint16_t coat = SR_RGB565(45, 66, 79);
    const uint16_t coat_light = SR_RGB565(69, 104, 110);
    const uint16_t stitch = SR_RGB565(50, 36, 42);
    const uint16_t felt = SR_RGB565(181, 103, 82);
    const uint16_t felt_light = SR_RGB565(218, 143, 105);
    const uint16_t patch = SR_RGB565(115, 76, 91);
    const uint16_t white = SR_RGB565(242, 224, 190);
    const uint16_t iris = SR_RGB565(79, 151, 145);
    const uint16_t pupil = SR_RGB565(30, 42, 47);
    const uint16_t mouth = SR_RGB565(67, 30, 39);
    const uint16_t tongue = SR_RGB565(221, 91, 91);
    const uint16_t gold = SR_RGB565(245, 188, 76);

    sr_fill(canvas, background);
    sr_ellipse(canvas, 80, 114, 64, 9, shadow);
    const int bx = pose->body_lean_x;
    const int by = pose->body_lean_y;
    sr_ellipse(canvas, 80 + bx, 120 + by, 65, 29, coat);
    sr_triangle(
        canvas,
        24 + bx,
        116 + by,
        51 + bx,
        91 + by,
        70 + bx,
        116 + by,
        coat_light);
    sr_triangle(
        canvas,
        136 + bx,
        116 + by,
        109 + bx,
        91 + by,
        90 + bx,
        116 + by,
        coat_light);

    /*
     * The felt ears are part of the performance, not static decoration:
     * perk for joy/excitement, droop for worry/sleep, and oppose each other
     * for thought/skepticism.  Tips remain comfortably inside the safe box.
     */
    int left_tip_y = 8;
    int right_tip_y = 15;
    switch (sr_discrete_expression(pose)) {
    case FACE_EXPRESSION_JOY:
    case FACE_EXPRESSION_EXCITED:
        left_tip_y = 6;
        right_tip_y = 7;
        break;
    case FACE_EXPRESSION_CONCERN:
    case FACE_EXPRESSION_SLEEPY:
        left_tip_y = 20;
        right_tip_y = 22;
        break;
    case FACE_EXPRESSION_THOUGHTFUL:
        left_tip_y = 7;
        right_tip_y = 23;
        break;
    case FACE_EXPRESSION_SKEPTICAL:
        left_tip_y = 22;
        right_tip_y = 8;
        break;
    case FACE_EXPRESSION_EMBARRASSED:
        left_tip_y = 17;
        right_tip_y = 21;
        break;
    default:
        break;
    }
    sr_triangle(canvas, 38, 38, 25, left_tip_y, 61, 26, stitch);
    sr_triangle(canvas, 122, 38, 139, right_tip_y, 103, 27, stitch);
    sr_triangle(
        canvas,
        39,
        34,
        30,
        left_tip_y + 6,
        56,
        27,
        felt);
    sr_triangle(
        canvas,
        121,
        34,
        135,
        right_tip_y + 5,
        106,
        28,
        felt_light);
    sr_ellipse(canvas, 80, 60, 49, 51, stitch);
    sr_ellipse(canvas, 80, 61, 45, 47, felt);
    sr_triangle(canvas, 39, 36, 62, 16, 78, 31, patch);
    sr_triangle(canvas, 67, 28, 101, 11, 121, 36, felt_light);
    sr_quad(canvas, 67, 18, 100, 13, 108, 30, 76, 35, patch);
    sr_ellipse(canvas, 67, 77, 18, 12, felt_light);
    sr_ellipse(canvas, 93, 77, 18, 12, felt_light);

    sr_felt_eye(canvas, pose, 0U, stitch, white, iris, pupil);
    sr_felt_eye(canvas, pose, 1U, stitch, white, iris, pupil);
    sr_brow(canvas, pose, 0U, 15, 3, stitch);
    sr_brow(canvas, pose, 1U, 15, 3, stitch);

    sr_triangle(canvas, 80, 68, 73, 76, 87, 76, patch);
    sr_line(canvas, 74, 78, 80, 82, stitch);
    sr_line(canvas, 80, 82, 86, 78, stitch);
    if (pose->cheek > 48U) {
        const int cheek_radius = 3 + (int)pose->cheek / 72;
        sr_ellipse(canvas, 42, 76, cheek_radius, 3, felt_light);
        sr_ellipse(canvas, 118, 76, cheek_radius, 3, felt_light);
        sr_line(canvas, 39, 76, 45, 76, stitch);
        sr_line(canvas, 115, 76, 121, 76, stitch);
    }
    sr_felt_mouth(canvas, pose, stitch, mouth, white, tongue);

    sr_curve(
        canvas,
        48 + bx,
        104 + by,
        80 + bx,
        111 + by,
        112 + bx,
        104 + by,
        4,
        patch);
    sr_trim_bits(
        canvas,
        59 + bx,
        107 + by,
        6,
        0,
        pose->detail_phase,
        gold,
        coat);
}

bool face_salvage_redux_render(
    face_salvage_redux_style_t style,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    if ((unsigned)style >= FACE_SALVAGE_REDUX_COUNT ||
        render_key == NULL || rgb565 == NULL ||
        pixel_capacity < FACE_SALVAGE_REDUX_PIXEL_COUNT) {
        return false;
    }
    face_salvage_redux_pose_t pose;
    if (!face_salvage_redux_resolve(
            style, render_key, sample_clock, &pose)) {
        return false;
    }
    sr_canvas_t canvas = {rgb565};
    switch (style) {
    case FACE_SALVAGE_REDUX_STORY_SCOUT:
        sr_draw_story_scout(&canvas, &pose);
        break;
    case FACE_SALVAGE_REDUX_POCKET_COURIER:
        sr_draw_pocket_courier(&canvas, &pose);
        break;
    case FACE_SALVAGE_REDUX_VELA_EYES:
        sr_draw_vela_eyes(&canvas, &pose);
        break;
    case FACE_SALVAGE_REDUX_KITE_ORACLE:
        sr_draw_kite_oracle(&canvas, &pose);
        break;
    case FACE_SALVAGE_REDUX_ORBIT_GARDENER:
        sr_draw_orbit_gardener(&canvas, &pose);
        break;
    case FACE_SALVAGE_REDUX_FELT_FAMILIAR:
        sr_draw_felt_familiar(&canvas, &pose);
        break;
    case FACE_SALVAGE_REDUX_COUNT:
    default:
        return false;
    }
    return true;
}

bool face_salvage_redux_render_legacy(
    uint8_t legacy_profile_id,
    const face_render_key_t *render_key,
    uint32_t sample_clock,
    uint16_t *rgb565,
    size_t pixel_capacity)
{
    face_salvage_redux_style_t style;
    return face_salvage_redux_from_legacy_id(
               legacy_profile_id, &style) &&
        face_salvage_redux_render(
            style,
            render_key,
            sample_clock,
            rgb565,
            pixel_capacity);
}
