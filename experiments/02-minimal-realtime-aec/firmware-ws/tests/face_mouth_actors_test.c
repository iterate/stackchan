#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_mouth_actors.h"

enum {
    CANARY_WORDS = 32,
    EXPRESSION_COUNT = 11,
    FIXED_CLOCK = 16000 * 5 + 211,
};

static uint16_t guarded[
    CANARY_WORDS + FACE_MOUTH_ACTORS_PIXEL_COUNT + CANARY_WORDS];
static uint16_t frames[EXPRESSION_COUNT][FACE_MOUTH_ACTORS_PIXEL_COUNT];
static uint16_t comparison[FACE_MOUTH_ACTORS_PIXEL_COUNT];

static face_render_key_t base_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 112U;
    key.controls.mouth_width = 154U;
    key.controls.mouth_round = 36U;
    key.controls.mouth_press = 7U;
    key.controls.mouth_teeth = 144U;
    key.controls.eye_left_open = 242U;
    key.controls.eye_right_open = 238U;
    key.controls.look_x = -4;
    key.controls.look_y = 2;
    key.controls.brow = 5;
    key.controls.expression = FACE_ACTIVITY_LISTENING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_E;
    key.phoneme = 7U;
    key.viseme_weight = 210U;
    key.audio_level = 136U;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_AA;
    key.viseme_blend = 37U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 5;
    key.mouth_corner_right = 9;
    key.tongue = 52U;
    key.cheek = 34U;
    key.eye_left_squint = 8U;
    key.eye_right_squint = 13U;
    key.brow_inner = 7;
    key.brow_outer_left = 3;
    key.brow_outer_right = -2;
    key.head_roll = 2;
    key.affect_valence = 11;
    key.affect_arousal = 128U;
    key.head_yaw = -3;
    key.head_pitch = 2;
    key.body_lean_x = 1;
    key.body_lean_y = -2;
    key.expression_weight = 255U;
    key.attention = 226U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    key.stage_expression = 0U;
    return key;
}

static uint32_t frame_hash(const uint16_t *frame)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0U;
         index < FACE_MOUTH_ACTORS_PIXEL_COUNT;
         ++index) {
        hash ^= frame[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t changed_region(
    const uint16_t *first,
    const uint16_t *second,
    face_mouth_actor_bounds_t bounds)
{
    size_t changed = 0U;
    for (size_t y = bounds.y;
         y < (size_t)bounds.y + bounds.height;
         ++y) {
        for (size_t x = bounds.x;
             x < (size_t)bounds.x + bounds.width;
             ++x) {
            const size_t index = y * FACE_MOUTH_ACTORS_WIDTH + x;
            changed += first[index] != second[index];
        }
    }
    return changed;
}

static size_t distinct_region(
    const uint16_t *frame, face_mouth_actor_bounds_t bounds)
{
    uint16_t seen[32];
    size_t count = 0U;
    for (size_t y = bounds.y;
         y < (size_t)bounds.y + bounds.height;
         ++y) {
        for (size_t x = bounds.x;
             x < (size_t)bounds.x + bounds.width;
             ++x) {
            const uint16_t color =
                frame[y * FACE_MOUTH_ACTORS_WIDTH + x];
            size_t match = 0U;
            while (match < count && seen[match] != color) {
                ++match;
            }
            if (match == count &&
                count < sizeof(seen) / sizeof(seen[0])) {
                seen[count++] = color;
            }
        }
    }
    return count;
}

static bool bounds_inside(
    face_mouth_actor_bounds_t bounds, unsigned margin)
{
    return bounds.width > 0U && bounds.height > 0U &&
           bounds.x >= margin && bounds.y >= margin &&
           (unsigned)bounds.x + bounds.width <=
               FACE_MOUTH_ACTORS_WIDTH - margin &&
           (unsigned)bounds.y + bounds.height <=
               FACE_MOUTH_ACTORS_HEIGHT - margin;
}

static void test_public_contract(void)
{
    assert(sizeof(face_render_key_t) == 40U);
    assert(FACE_MOUTH_ACTORS_FRAME_BYTES == 38400);
    assert(FACE_MOUTH_ACTORS_CONTEXT_BYTES == 0);
    assert(face_mouth_actors_profile_count() == FACE_MOUTH_ACTOR_COUNT);
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        assert(face_mouth_actors_profile_slug(
                   (face_mouth_actor_profile_t)profile) != NULL);
        assert(face_mouth_actors_profile_name(
                   (face_mouth_actor_profile_t)profile) != NULL);
    }
    assert(face_mouth_actors_profile_slug(
               (face_mouth_actor_profile_t)FACE_MOUTH_ACTOR_COUNT) == NULL);
    assert(face_mouth_actors_profile_name(
               (face_mouth_actor_profile_t)FACE_MOUTH_ACTOR_COUNT) == NULL);

    face_render_key_t key = base_key();
    uint16_t *frame = guarded + CANARY_WORDS;
    assert(!face_mouth_actors_render(
        (face_mouth_actor_profile_t)FACE_MOUTH_ACTOR_COUNT,
        &key, 0U, frame, FACE_MOUTH_ACTORS_PIXEL_COUNT));
    assert(!face_mouth_actors_render(
        FACE_MOUTH_ACTOR_PRESTON,
        NULL, 0U, frame, FACE_MOUTH_ACTORS_PIXEL_COUNT));
    assert(!face_mouth_actors_render(
        FACE_MOUTH_ACTOR_PRESTON,
        &key, 0U, NULL, FACE_MOUTH_ACTORS_PIXEL_COUNT));
    assert(!face_mouth_actors_render(
        FACE_MOUTH_ACTOR_PRESTON,
        &key, 0U, frame, FACE_MOUTH_ACTORS_PIXEL_COUNT - 1U));
}

static void test_complete_key_is_preserved_and_consumed(void)
{
    const face_render_key_t key = base_key();
    face_mouth_actor_pose_t baseline;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_RIBBON, &key, FIXED_CLOCK, &baseline));
    assert(memcmp(&baseline.source, &key, sizeof(key)) == 0);
    const uint8_t *baseline_bytes = (const uint8_t *)&key;
    for (size_t byte_index = 0U;
         byte_index < sizeof(key);
         ++byte_index) {
        face_render_key_t changed = key;
        uint8_t *changed_bytes = (uint8_t *)&changed;
        changed_bytes[byte_index] ^=
            (uint8_t)(0x5aU + (uint8_t)byte_index);
        face_mouth_actor_pose_t pose;
        assert(face_mouth_actors_resolve(
            FACE_MOUTH_ACTOR_RIBBON, &changed, FIXED_CLOCK, &pose));
        assert(memcmp(&pose.source, &changed, sizeof(changed)) == 0);
        assert(pose.input_signature != baseline.input_signature);
        assert(changed_bytes[byte_index] != baseline_bytes[byte_index]);
    }
}

static void test_bounds_canaries_and_eye_survival(void)
{
    const uint16_t canary = 0xa55aU;
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        for (size_t expression = 0U;
             expression < EXPRESSION_COUNT;
             ++expression) {
            for (size_t index = 0U;
                 index < sizeof(guarded) / sizeof(guarded[0]);
                 ++index) {
                guarded[index] = canary;
            }
            face_render_key_t key = base_key();
            key.stage_expression = (uint8_t)expression;
            key.expression_weight = 255U;
            face_mouth_actor_landmarks_t marks;
            assert(face_mouth_actors_render_checked(
                (face_mouth_actor_profile_t)profile,
                &key,
                FIXED_CLOCK,
                guarded + CANARY_WORDS,
                FACE_MOUTH_ACTORS_PIXEL_COUNT,
                &marks));
            assert(bounds_inside(marks.face, 4U));
            assert(bounds_inside(marks.left_eye, 8U));
            assert(bounds_inside(marks.right_eye, 8U));
            assert(bounds_inside(marks.mouth, 8U));
            assert(marks.left_eye.x < marks.right_eye.x);
            assert(marks.left_eye.y < marks.mouth.y);
            assert(marks.right_eye.y < marks.mouth.y);
            assert(distinct_region(
                       guarded + CANARY_WORDS, marks.left_eye) >= 3U);
            assert(distinct_region(
                       guarded + CANARY_WORDS, marks.right_eye) >= 3U);
            for (size_t index = 0U; index < CANARY_WORDS; ++index) {
                assert(guarded[index] == canary);
                assert(guarded[
                           CANARY_WORDS +
                           FACE_MOUTH_ACTORS_PIXEL_COUNT + index] ==
                       canary);
            }
        }

        face_render_key_t closed = base_key();
        closed.controls.eye_left_open = 0U;
        closed.controls.eye_right_open = 0U;
        closed.eye_left_squint = 255U;
        closed.eye_right_squint = 255U;
        closed.stage_expression = 8U;
        face_mouth_actor_landmarks_t marks;
        assert(face_mouth_actors_render_checked(
            (face_mouth_actor_profile_t)profile,
            &closed,
            FIXED_CLOCK,
            comparison,
            FACE_MOUTH_ACTORS_PIXEL_COUNT,
            &marks));
        assert(distinct_region(comparison, marks.left_eye) >= 3U);
        assert(distinct_region(comparison, marks.right_eye) >= 3U);
    }
}

static void test_adversarial_key_bounds(void)
{
    const uint16_t canary = 0x6db6U;
    uint32_t random = 0x13579bdfU;
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        for (uint32_t iteration = 0U; iteration < 128U; ++iteration) {
            face_render_key_t key;
            uint8_t *bytes = (uint8_t *)&key;
            for (size_t byte = 0U; byte < sizeof(key); ++byte) {
                random = random * 1664525U + 1013904223U;
                bytes[byte] = (uint8_t)(random >> 24);
            }
            for (size_t index = 0U;
                 index < sizeof(guarded) / sizeof(guarded[0]);
                 ++index) {
                guarded[index] = canary;
            }
            assert(face_mouth_actors_render(
                (face_mouth_actor_profile_t)profile,
                &key,
                random ^ (iteration * 977U),
                guarded + CANARY_WORDS,
                FACE_MOUTH_ACTORS_PIXEL_COUNT));
            for (size_t index = 0U; index < CANARY_WORDS; ++index) {
                assert(guarded[index] == canary);
                assert(guarded[
                           CANARY_WORDS +
                           FACE_MOUTH_ACTORS_PIXEL_COUNT + index] ==
                       canary);
            }
        }
    }
}

static void test_all_expressions_are_distinct(void)
{
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        uint32_t hashes[EXPRESSION_COUNT];
        for (size_t expression = 0U;
             expression < EXPRESSION_COUNT;
             ++expression) {
            face_render_key_t key = base_key();
            key.stage_expression = (uint8_t)expression;
            key.expression_weight = 255U;
            assert(face_mouth_actors_render(
                (face_mouth_actor_profile_t)profile,
                &key,
                FIXED_CLOCK,
                frames[expression],
                FACE_MOUTH_ACTORS_PIXEL_COUNT));
            hashes[expression] = frame_hash(frames[expression]);
        }
        size_t minimum_change = FACE_MOUTH_ACTORS_PIXEL_COUNT;
        for (size_t first = 0U; first < EXPRESSION_COUNT; ++first) {
            for (size_t second = first + 1U;
                 second < EXPRESSION_COUNT;
                 ++second) {
                assert(hashes[first] != hashes[second]);
                size_t changed = 0U;
                for (size_t pixel = 0U;
                     pixel < FACE_MOUTH_ACTORS_PIXEL_COUNT;
                     ++pixel) {
                    changed +=
                        frames[first][pixel] != frames[second][pixel];
                }
                if (changed < minimum_change) {
                    minimum_change = changed;
                }
            }
        }
        if (minimum_change < 95U) {
            fprintf(
                stderr,
                "%s weak expression pair: %zu changed pixels\n",
                face_mouth_actors_profile_slug(
                    (face_mouth_actor_profile_t)profile),
                minimum_change);
        }
        assert(minimum_change >= 95U);
    }
}

static void test_mouth_response_and_emotion_coupling(void)
{
    static const uint8_t VISEMES[5] = {
        FACE_VISEME_AA,
        FACE_VISEME_O,
        FACE_VISEME_PP,
        FACE_VISEME_FF,
        FACE_VISEME_TH,
    };
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        face_mouth_actor_landmarks_t marks;
        face_render_key_t closed = base_key();
        closed.controls.mouth_open = 0U;
        closed.controls.mouth_press = 255U;
        closed.viseme = FACE_VISEME_PP;
        closed.viseme_weight = 255U;
        closed.audio_level = 0U;
        assert(face_mouth_actors_render_checked(
            (face_mouth_actor_profile_t)profile,
            &closed,
            FIXED_CLOCK,
            frames[0],
            FACE_MOUTH_ACTORS_PIXEL_COUNT,
            &marks));
        face_render_key_t open = closed;
        open.controls.mouth_open = 255U;
        open.controls.mouth_press = 0U;
        open.controls.mouth_round = 10U;
        open.viseme = FACE_VISEME_AA;
        open.audio_level = 255U;
        assert(face_mouth_actors_render(
            (face_mouth_actor_profile_t)profile,
            &open,
            FIXED_CLOCK,
            frames[1],
            FACE_MOUTH_ACTORS_PIXEL_COUNT));
        const size_t open_change =
            changed_region(frames[0], frames[1], marks.mouth);
        assert(open_change >= 70U);

        uint32_t viseme_hashes[5];
        for (size_t viseme = 0U; viseme < 5U; ++viseme) {
            face_render_key_t key = base_key();
            key.viseme = VISEMES[viseme];
            key.viseme_secondary = FACE_VISEME_NONE;
            key.viseme_blend = 0U;
            key.viseme_weight = 255U;
            key.controls.mouth_open =
                VISEMES[viseme] == FACE_VISEME_PP ? 0U : 130U;
            key.controls.mouth_press =
                VISEMES[viseme] == FACE_VISEME_PP ? 255U : 0U;
            key.controls.mouth_round =
                VISEMES[viseme] == FACE_VISEME_O ? 255U : 12U;
            key.tongue =
                VISEMES[viseme] == FACE_VISEME_TH ? 255U : 0U;
            assert(face_mouth_actors_render(
                (face_mouth_actor_profile_t)profile,
                &key,
                FIXED_CLOCK,
                frames[viseme],
                FACE_MOUTH_ACTORS_PIXEL_COUNT));
            viseme_hashes[viseme] = frame_hash(frames[viseme]);
        }
        for (size_t first = 0U; first < 5U; ++first) {
            for (size_t second = first + 1U; second < 5U; ++second) {
                assert(viseme_hashes[first] != viseme_hashes[second]);
            }
        }

        face_render_key_t joyful = base_key();
        joyful.stage_expression = 2U;
        joyful.expression_weight = 255U;
        joyful.viseme = FACE_VISEME_AA;
        assert(face_mouth_actors_render(
            (face_mouth_actor_profile_t)profile,
            &joyful,
            FIXED_CLOCK,
            frames[0],
            FACE_MOUTH_ACTORS_PIXEL_COUNT));
        face_render_key_t concerned = joyful;
        concerned.stage_expression = 3U;
        assert(face_mouth_actors_render(
            (face_mouth_actor_profile_t)profile,
            &concerned,
            FIXED_CLOCK,
            frames[1],
            FACE_MOUTH_ACTORS_PIXEL_COUNT));
        assert(changed_region(frames[0], frames[1], marks.mouth) >= 20U);
    }
}

static void test_corner_grammar_coarticulation_and_phases(void)
{
    face_render_key_t key = base_key();
    key.controls.mouth_open = 0U;
    key.controls.mouth_press = 255U;
    key.viseme = FACE_VISEME_PP;
    key.viseme_weight = 255U;
    face_mouth_actor_pose_t pressed;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_JALI, &key, FIXED_CLOCK, &pressed));
    assert(pressed.mouth_open <= 3);
    assert(pressed.mouth_width > pressed.mouth_open);

    key = base_key();
    key.stage_expression = 2U;
    key.expression_weight = 255U;
    face_mouth_actor_pose_t joyful;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_RIBBON, &key, FIXED_CLOCK, &joyful));
    assert(joyful.mouth_corner_left < 0);
    assert(joyful.mouth_corner_right < 0);

    key.stage_expression = 3U;
    face_mouth_actor_pose_t concerned;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_RIBBON, &key, FIXED_CLOCK, &concerned));
    assert(concerned.mouth_corner_left > 0);
    assert(concerned.mouth_corner_right > 0);

    key.stage_expression = 10U;
    face_mouth_actor_pose_t embarrassed;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_RIBBON, &key, FIXED_CLOCK, &embarrassed));
    int corner_difference =
        embarrassed.mouth_corner_left -
        embarrassed.mouth_corner_right;
    if (corner_difference < 0) {
        corner_difference = -corner_difference;
    }
    assert(corner_difference >= 6);

    face_render_key_t vowel = base_key();
    vowel.controls.mouth_open = 220U;
    vowel.controls.mouth_round = 0U;
    vowel.controls.mouth_press = 0U;
    vowel.viseme = FACE_VISEME_AA;
    vowel.viseme_secondary = FACE_VISEME_E;
    vowel.viseme_weight = 255U;
    vowel.viseme_blend = 0U;
    face_mouth_actor_pose_t aa;
    face_mouth_actor_pose_t blend;
    face_mouth_actor_pose_t ee;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_ORIGAMI, &vowel, FIXED_CLOCK, &aa));
    vowel.viseme_blend = 128U;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_ORIGAMI, &vowel, FIXED_CLOCK, &blend));
    vowel.viseme_blend = 255U;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_ORIGAMI, &vowel, FIXED_CLOCK, &ee));
    assert(
        blend.mouth_open <=
            (aa.mouth_open > ee.mouth_open
                 ? aa.mouth_open
                 : ee.mouth_open) &&
        blend.mouth_open >=
            (aa.mouth_open < ee.mouth_open
                 ? aa.mouth_open
                 : ee.mouth_open));
    assert(blend.mouth_width > blend.mouth_open);

    vowel.controls.mouth_round = 255U;
    vowel.viseme = FACE_VISEME_O;
    vowel.viseme_secondary = FACE_VISEME_NONE;
    vowel.viseme_blend = 0U;
    face_mouth_actor_pose_t rounded;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_ORIGAMI, &vowel, FIXED_CLOCK, &rounded));
    assert(rounded.mouth_round > aa.mouth_round);
    assert(rounded.mouth_width * 2 >= rounded.mouth_open * 3);

    face_render_key_t active_key = base_key();
    active_key.controls.mouth_open = 240U;
    active_key.viseme = FACE_VISEME_AA;
    active_key.viseme_weight = 255U;
    active_key.speech_phase = FACE_SPEECH_ACTIVE;
    face_mouth_actor_pose_t active;
    face_mouth_actor_pose_t anticipation;
    face_mouth_actor_pose_t settle;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_PRESTON, &active_key, FIXED_CLOCK, &active));
    active_key.speech_phase = FACE_SPEECH_STARTING;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_PRESTON,
        &active_key,
        FIXED_CLOCK,
        &anticipation));
    active_key.speech_phase = FACE_SPEECH_ENDING;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_PRESTON, &active_key, FIXED_CLOCK, &settle));
    assert(anticipation.mouth_open < active.mouth_open);
    assert(settle.mouth_open < active.mouth_open);
    assert(anticipation.mouth_open > pressed.mouth_open);

    face_render_key_t quiet_key = base_key();
    quiet_key.controls.mouth_open = 0U;
    quiet_key.controls.mouth_press = 255U;
    quiet_key.viseme = FACE_VISEME_PP;
    quiet_key.viseme_weight = 255U;
    quiet_key.audio_level = 0U;
    face_mouth_actor_pose_t quiet;
    assert(face_mouth_actors_resolve(
        FACE_MOUTH_ACTOR_TEETH_TONGUE,
        &quiet_key,
        FIXED_CLOCK,
        &quiet));
    assert(active.cheek > quiet.cheek);
    assert(
        active.eye_left_open < quiet.eye_left_open ||
        active.brow_left > quiet.brow_left ||
        active.head_y != quiet.head_y);
}

static void test_determinism_clock_and_profile_signatures(void)
{
    /*
     * Exact hashes pin fixed-point rasterization and palette output.  Populate
     * EXPECTED only after visual approval of the native sheets.
     */
    static const uint32_t EXPECTED[FACE_MOUTH_ACTOR_COUNT] = {
        2424441953U,
        2840869055U,
        2051127888U,
        905741838U,
        3121249488U,
        4161723993U,
    };
    uint32_t profile_hashes[FACE_MOUTH_ACTOR_COUNT];
    bool signatures_match = true;
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        face_render_key_t key = base_key();
        key.stage_expression = 9U;
        assert(face_mouth_actors_render(
            (face_mouth_actor_profile_t)profile,
            &key,
            FIXED_CLOCK,
            frames[0],
            FACE_MOUTH_ACTORS_PIXEL_COUNT));
        assert(face_mouth_actors_render(
            (face_mouth_actor_profile_t)profile,
            &key,
            FIXED_CLOCK,
            frames[1],
            FACE_MOUTH_ACTORS_PIXEL_COUNT));
        assert(memcmp(
                   frames[0],
                   frames[1],
                   sizeof(frames[0])) == 0);
        profile_hashes[profile] = frame_hash(frames[0]);
        if (profile_hashes[profile] != EXPECTED[profile]) {
            fprintf(
                stderr,
                "signature %s: expected %uU, got %uU\n",
                face_mouth_actors_profile_slug(
                    (face_mouth_actor_profile_t)profile),
                EXPECTED[profile],
                profile_hashes[profile]);
            signatures_match = false;
        }
        bool clock_changed = false;
        for (uint32_t step = 1U; step <= 12U; ++step) {
            assert(face_mouth_actors_render(
                (face_mouth_actor_profile_t)profile,
                &key,
                FIXED_CLOCK + step * 877U,
                comparison,
                FACE_MOUTH_ACTORS_PIXEL_COUNT));
            clock_changed |=
                frame_hash(comparison) != profile_hashes[profile];
        }
        assert(clock_changed);
    }
    for (size_t first = 0U;
         first < FACE_MOUTH_ACTOR_COUNT;
         ++first) {
        for (size_t second = first + 1U;
             second < FACE_MOUTH_ACTOR_COUNT;
             ++second) {
            assert(profile_hashes[first] != profile_hashes[second]);
        }
    }
    assert(signatures_match);
}

static void test_head_cues_do_not_translate_the_chassis(void)
{
    for (size_t profile = 0U;
         profile < FACE_MOUTH_ACTOR_COUNT;
         ++profile) {
        face_render_key_t first = base_key();
        face_render_key_t second = first;
        face_mouth_actor_pose_t first_pose;
        face_mouth_actor_pose_t second_pose;

        /*
         * These adjacent values cross the old integer head-y boundary.
         * A continuous cue must affect only local facial controls, never move
         * the entire painted silhouette by one raster row.
         */
        first.head_pitch = 21;
        second.head_pitch = 22;
        assert(face_mouth_actors_resolve(
            (face_mouth_actor_profile_t)profile,
            &first,
            FIXED_CLOCK,
            &first_pose));
        assert(face_mouth_actors_resolve(
            (face_mouth_actor_profile_t)profile,
            &second,
            FIXED_CLOCK,
            &second_pose));
        assert(first_pose.head_x == 80 && first_pose.head_y == 58);
        assert(second_pose.head_x == 80 && second_pose.head_y == 58);
        assert(first_pose.head_roll == 0 && second_pose.head_roll == 0);

        assert(face_mouth_actors_render(
            (face_mouth_actor_profile_t)profile,
            &first,
            FIXED_CLOCK,
            frames[0],
            FACE_MOUTH_ACTORS_PIXEL_COUNT));
        assert(face_mouth_actors_render(
            (face_mouth_actor_profile_t)profile,
            &second,
            FIXED_CLOCK,
            frames[1],
            FACE_MOUTH_ACTORS_PIXEL_COUNT));
        const face_mouth_actor_bounds_t full_frame = {
            0,
            0,
            FACE_MOUTH_ACTORS_WIDTH,
            FACE_MOUTH_ACTORS_HEIGHT,
        };
        assert(changed_region(frames[0], frames[1], full_frame) <
               FACE_MOUTH_ACTORS_PIXEL_COUNT / 100U);
    }
}

int main(void)
{
    test_public_contract();
    test_complete_key_is_preserved_and_consumed();
    test_bounds_canaries_and_eye_survival();
    test_adversarial_key_bounds();
    test_all_expressions_are_distinct();
    test_mouth_response_and_emotion_coupling();
    test_corner_grammar_coarticulation_and_phases();
    test_head_cues_do_not_translate_the_chassis();
    test_determinism_clock_and_profile_signatures();
    puts("face_mouth_actors_test: ok");
    return 0;
}
