#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "face_render.h"

enum {
    GUARD_WORDS = 8,
};

typedef struct {
    uint32_t before[GUARD_WORDS];
    uint16_t pixels[FACE_RENDER_PIXEL_COUNT];
    uint32_t after[GUARD_WORDS];
} guarded_frame_t;

static uint32_t frame_hash(const uint16_t *pixels)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < FACE_RENDER_PIXEL_COUNT; ++index) {
        hash ^= pixels[index];
        hash *= 16777619U;
    }
    return hash;
}

static void initialise_guard(guarded_frame_t *frame)
{
    memset(frame, 0xa5, sizeof(*frame));
    for (size_t index = 0; index < GUARD_WORDS; ++index) {
        frame->before[index] = 0x51a7c0deU + (uint32_t)index;
        frame->after[index] = 0x0ddba11U + (uint32_t)index;
    }
}

static void assert_guard_unchanged(const guarded_frame_t *frame)
{
    for (size_t index = 0; index < GUARD_WORDS; ++index) {
        assert(frame->before[index] == 0x51a7c0deU + index);
        assert(frame->after[index] == 0x0ddba11U + index);
    }
}

static face_render_key_t expressive_key(void)
{
    face_render_key_t key;
    memset(&key, 0, sizeof(key));
    key.controls.mouth_open = 188;
    key.controls.mouth_width = 176;
    key.controls.mouth_round = 72;
    key.controls.mouth_press = 18;
    key.controls.mouth_teeth = 96;
    key.controls.eye_left_open = 238;
    key.controls.eye_right_open = 224;
    key.controls.look_x = -12;
    key.controls.look_y = 8;
    key.controls.brow = 26;
    key.controls.expression = FACE_ACTIVITY_SPEAKING;
    key.controls.flags = FACE_KEYFRAME_FLAG_SPEAKING;
    key.viseme = FACE_VISEME_AA;
    key.phoneme = 0U;
    key.viseme_weight = 220;
    key.audio_level = 174;
    key.viseme_set = FACE_VISEME_SET_OVR15;
    key.viseme_secondary = FACE_VISEME_E;
    key.viseme_blend = 42U;
    key.speech_phase = FACE_SPEECH_ACTIVE;
    key.mouth_corner_left = 28;
    key.mouth_corner_right = 28;
    key.cheek = 36U;
    key.eye_left_squint = 14U;
    key.eye_right_squint = 18U;
    key.brow_inner = 12;
    key.brow_outer_left = 6;
    key.brow_outer_right = 8;
    key.affect_valence = 32;
    key.affect_arousal = 174U;
    key.head_yaw = -8;
    key.head_pitch = 5;
    key.expression_weight = 220U;
    key.attention = 210U;
    key.schema_version = FACE_RENDER_KEY_SCHEMA_VERSION;
    return key;
}

int main(void)
{
    assert(sizeof(face_render_info_t) == 16U);
    assert(sizeof(face_render_key_t) == 40U);
    assert(face_render_profile_count() == FACE_RENDER_PROFILE_COUNT);
    assert(face_render_profile_count() >= 40U);

    unsigned int family_counts[FACE_RENDER_FAMILY_CYBER + 1U] = {0};
    unsigned int sprite_count = 0U;
    unsigned int polygon_count = 0U;
    unsigned int idle_count = 0U;
    guarded_frame_t first;
    guarded_frame_t second;
    face_render_key_t key = expressive_key();
    unsigned int clock_sensitive = 0U;
    unsigned int mouth_sensitive = 0U;
    unsigned int viseme_sensitive = 0U;

    for (size_t raw_profile = 0;
         raw_profile < face_render_profile_count();
         ++raw_profile) {
        const face_render_profile_t profile =
            (face_render_profile_t)raw_profile;
        const char *slug = face_render_profile_slug(profile);
        const char *name = face_render_profile_name(profile);
        const char *family_name =
            face_render_profile_family_name(profile);
        assert(slug != NULL && slug[0] != '\0');
        assert(name != NULL && name[0] != '\0');
        assert(family_name != NULL && family_name[0] != '\0');
        for (size_t earlier = 0; earlier < raw_profile; ++earlier) {
            assert(strcmp(
                       slug,
                       face_render_profile_slug(
                           (face_render_profile_t)earlier)) != 0);
        }

        face_render_info_t info;
        memset(&info, 0, sizeof(info));
        assert(face_render_profile_info(profile, &info));
        assert(info.width == FACE_RENDER_WIDTH);
        assert(info.height == FACE_RENDER_HEIGHT);
        assert(info.work_width > 0 && info.work_width <= info.width);
        assert(info.work_height > 0 && info.work_height <= info.height);
        assert(info.framebuffer_bytes == FACE_RENDER_FRAME_BYTES);
        assert(info.family <= FACE_RENDER_FAMILY_CYBER);
        assert(info.mouth_kind <= FACE_RENDER_MOUTH_SDF);
        assert(info.estimated_ops_per_pixel > 0U);
        ++family_counts[info.family];
        sprite_count +=
            (info.flags & FACE_RENDER_FLAG_SPRITE_MOUTH) != 0U;
        polygon_count +=
            (info.flags & FACE_RENDER_FLAG_POLYGON_MOUTH) != 0U;
        idle_count +=
            (info.flags & FACE_RENDER_FLAG_IDLE_MOTION) != 0U;

        initialise_guard(&first);
        initialise_guard(&second);
        assert(face_render_frame(
            profile, &key, 16000U * 7U + 211U,
            first.pixels, FACE_RENDER_PIXEL_COUNT));
        assert(face_render_frame(
            profile, &key, 16000U * 7U + 211U,
            second.pixels, FACE_RENDER_PIXEL_COUNT));
        assert_guard_unchanged(&first);
        assert_guard_unchanged(&second);
        const uint32_t baseline_hash = frame_hash(first.pixels);
        assert(baseline_hash == frame_hash(second.pixels));

        assert(face_render_frame(
            profile, &key, 16000U * 9U + 431U,
            second.pixels, FACE_RENDER_PIXEL_COUNT));
        clock_sensitive += baseline_hash != frame_hash(second.pixels);

        face_render_key_t changed = key;
        changed.controls.mouth_open = 12U;
        changed.controls.mouth_width = 56U;
        changed.controls.mouth_round = 230U;
        changed.audio_level = 8U;
        assert(face_render_frame(
            profile, &changed, 16000U * 7U + 211U,
            second.pixels, FACE_RENDER_PIXEL_COUNT));
        mouth_sensitive += baseline_hash != frame_hash(second.pixels);

        changed = key;
        changed.viseme = FACE_VISEME_PP;
        changed.phoneme = 5U;
        assert(face_render_frame(
            profile, &changed, 16000U * 7U + 211U,
            second.pixels, FACE_RENDER_PIXEL_COUNT));
        viseme_sensitive += baseline_hash != frame_hash(second.pixels);
    }

    for (size_t family = 0;
         family < sizeof(family_counts) / sizeof(family_counts[0]);
         ++family) {
        assert(family_counts[family] >= 6U);
    }
    assert(sprite_count >= 5U);
    assert(polygon_count >= 5U);
    assert(idle_count == FACE_RENDER_PROFILE_COUNT);
    assert(clock_sensitive >= 30U);
    assert(mouth_sensitive >= 30U);
    assert(viseme_sensitive >= 5U);

    face_render_info_t info;
    assert(!face_render_profile_info(FACE_RENDER_PROFILE_COUNT, &info));
    assert(face_render_profile_slug(FACE_RENDER_PROFILE_COUNT) == NULL);
    assert(face_render_profile_name(FACE_RENDER_PROFILE_COUNT) == NULL);
    assert(face_render_profile_family_name(FACE_RENDER_PROFILE_COUNT) == NULL);
    assert(!face_render_frame(
        FACE_RENDER_PROFILE_COUNT, &key, 0U,
        first.pixels, FACE_RENDER_PIXEL_COUNT));
    assert(!face_render_frame(
        FACE_RENDER_EGA_QUEST, NULL, 0U,
        first.pixels, FACE_RENDER_PIXEL_COUNT));
    assert(!face_render_frame(
        FACE_RENDER_EGA_QUEST, &key, 0U,
        first.pixels, FACE_RENDER_PIXEL_COUNT - 1U));

    printf(
        "face_render_test: PASS (%zu profiles, %u clock-sensitive, "
        "%u mouth-sensitive, %u viseme-sensitive)\n",
        face_render_profile_count(), clock_sensitive,
        mouth_sensitive, viseme_sensitive);
    return 0;
}
