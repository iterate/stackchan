#include "../src/fable_motion.h"

#include "test_support.h"

enum {
    MS = FABLE_SAMPLES_PER_MS,
    MINUTE = 60 * 1000 * MS,
};

static fable_keyframe_t make_kf(uint8_t activity, uint8_t speaking)
{
    fable_keyframe_t kf;
    memset(&kf, 0, sizeof(kf));
    kf.eye_left_open = 255;
    kf.eye_right_open = 255;
    kf.expression = activity;
    kf.flags = speaking ? FABLE_KEYFRAME_FLAG_SPEAKING : 0U;
    if (speaking) {
        kf.mouth_open = 140;
        kf.mouth_width = 160;
    }
    return kf;
}

static void test_determinism(void)
{
    const uint32_t clocks[] = { 0U, 1U, 12345U, 999999U, 123456789U,
                                4294967295U, 4294000000U };
    for (uint32_t pi = 0; pi < FABLE_PERSONA_COUNT; pi++) {
        const fable_persona_t *p = fable_persona_at(pi);
        const fable_keyframe_t kf = make_kf(FABLE_ACTIVITY_IDLE, 0);
        for (size_t ci = 0; ci < sizeof(clocks) / sizeof(clocks[0]);
             ci++) {
            fable_motion_pose_t a;
            fable_motion_pose_t b;
            memset(&a, 0xaa, sizeof(a));
            memset(&b, 0x55, sizeof(b));
            fable_motion_eval(p, &kf, clocks[ci], &a);
            /* Interleave other evaluations to prove statelessness. */
            fable_motion_pose_t scratch;
            fable_motion_eval(p, NULL, clocks[ci] / 2U + 7U, &scratch);
            fable_motion_eval(fable_persona_at((pi + 1U) % 5U), &kf,
                              clocks[ci] + 1U, &scratch);
            fable_motion_eval(p, &kf, clocks[ci], &b);
            CHECK(memcmp(&a, &b, sizeof(a)) == 0,
                  "persona %s clock %u not deterministic", p->slug,
                  clocks[ci]);
        }
    }
    /* NULL persona falls back to CALM; NULL keyframe is silent idle. */
    fable_motion_pose_t a;
    fable_motion_pose_t b;
    fable_motion_eval(NULL, NULL, 777777U, &a);
    fable_motion_eval(&FABLE_PERSONA_CALM, NULL, 777777U, &b);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0, "NULL persona fallback");
}

static void check_pose_bounds(const fable_motion_pose_t *pose,
                              const char *tag, uint32_t clock)
{
    CHECK(pose->lid_left_q10 <= FABLE_ONE, "%s lid_l %u at %u", tag,
          pose->lid_left_q10, clock);
    CHECK(pose->lid_right_q10 <= FABLE_ONE, "%s lid_r %u at %u", tag,
          pose->lid_right_q10, clock);
    CHECK(pose->eye_x_q2 >= -160 && pose->eye_x_q2 <= 160,
          "%s eye_x %d at %u", tag, pose->eye_x_q2, clock);
    CHECK(pose->eye_y_q2 >= -120 && pose->eye_y_q2 <= 120,
          "%s eye_y %d at %u", tag, pose->eye_y_q2, clock);
    CHECK(pose->head_x_q2 >= -160 && pose->head_x_q2 <= 160,
          "%s head_x %d at %u", tag, pose->head_x_q2, clock);
    CHECK(pose->head_y_q2 >= -120 && pose->head_y_q2 <= 120,
          "%s head_y %d at %u", tag, pose->head_y_q2, clock);
    CHECK(pose->lid_tilt >= -32 && pose->lid_tilt <= 32,
          "%s lid_tilt %d at %u", tag, pose->lid_tilt, clock);
    CHECK(pose->brow_left >= -64 && pose->brow_left <= 64,
          "%s brow_l %d at %u", tag, pose->brow_left, clock);
    CHECK(pose->brow_right >= -64 && pose->brow_right <= 64,
          "%s brow_r %d at %u", tag, pose->brow_right, clock);
    CHECK(pose->stretch >= -32 && pose->stretch <= 32,
          "%s stretch %d at %u", tag, pose->stretch, clock);
    CHECK(pose->act < FABLE_ACT_COUNT, "%s act %u at %u", tag,
          pose->act, clock);
}

static void test_bounds(void)
{
    fable_keyframe_t frames[6];
    frames[0] = make_kf(FABLE_ACTIVITY_IDLE, 0);
    frames[1] = make_kf(FABLE_ACTIVITY_LISTENING, 0);
    frames[2] = make_kf(FABLE_ACTIVITY_THINKING, 0);
    frames[3] = make_kf(FABLE_ACTIVITY_SPEAKING, 1);
    /* Hostile keyframe: every field at an extreme. */
    memset(&frames[4], 0xff, sizeof(frames[4]));
    memset(&frames[5], 0x00, sizeof(frames[5]));
    frames[5].look_x = -128;
    frames[5].look_y = -128;
    frames[5].brow = -128;

    for (uint32_t pi = 0; pi < FABLE_PERSONA_COUNT; pi++) {
        const fable_persona_t *p = fable_persona_at(pi);
        for (size_t fi = 0; fi < sizeof(frames) / sizeof(frames[0]);
             fi++) {
            for (uint32_t clock = 0; clock < 4U * (uint32_t)MINUTE;
                 clock += 1600U + 7U) {
                fable_motion_pose_t pose;
                fable_motion_eval(p, &frames[fi], clock, &pose);
                check_pose_bounds(&pose, p->slug, clock);
            }
        }
    }
}

/* Count closing edges: openness crossing below the given threshold. */
static uint32_t count_blinks(const fable_persona_t *p,
                             const fable_keyframe_t *kf,
                             uint32_t duration, uint32_t threshold_q10)
{
    uint32_t blinks = 0;
    int below = 0;
    for (uint32_t clock = 0; clock < duration; clock += 8U * MS) {
        fable_motion_pose_t pose;
        fable_motion_eval(p, kf, clock, &pose);
        const int now_below = pose.lid_left_q10 < threshold_q10;
        if (now_below && !below) {
            blinks++;
        }
        below = now_below;
    }
    return blinks;
}

static void test_blink_rate(void)
{
    const fable_keyframe_t idle = make_kf(FABLE_ACTIVITY_IDLE, 0);
    for (uint32_t pi = 0; pi < FABLE_PERSONA_COUNT; pi++) {
        const fable_persona_t *p = fable_persona_at(pi);
        const uint32_t duration = 10U * (uint32_t)MINUTE;
        const uint32_t blinks = count_blinks(p, &idle, duration, 480U);
        const uint32_t expected =
            (10U * 60U * 1000U) / p->blink_slot_ms;
        CHECK(blinks >= (expected * 7U) / 10U,
              "%s blinks too rarely: %u vs expected %u", p->slug,
              blinks, expected);
        CHECK(blinks <= (expected * 19U) / 10U,
              "%s blinks too often: %u vs expected %u", p->slug,
              blinks, expected);
    }
}

static void test_blink_shape(void)
{
    /* Find one full blink for CALM and verify fast-close/slow-open. */
    const fable_keyframe_t idle = make_kf(FABLE_ACTIVITY_IDLE, 0);
    const fable_persona_t *p = &FABLE_PERSONA_CALM;
    uint32_t start = 0;
    for (uint32_t clock = 0; clock < 2U * (uint32_t)MINUTE;
         clock += MS) {
        fable_motion_pose_t pose;
        fable_motion_eval(p, &idle, clock, &pose);
        if (pose.lid_left_q10 < 100U) {
            start = clock;
            break;
        }
    }
    CHECK(start != 0U, "no deep blink found for calm");
    if (start == 0U) {
        return;
    }
    /* Walk backwards to the moment the lid left its resting band. */
    uint32_t close_begin = start;
    while (close_begin > 0U) {
        fable_motion_pose_t pose;
        fable_motion_eval(p, &idle, close_begin - MS, &pose);
        if (pose.lid_left_q10 > 900U) {
            break;
        }
        close_begin -= MS;
    }
    /* Walk forward until it is back near rest. */
    uint32_t open_end = start;
    while (open_end < start + 1600U * MS) {
        fable_motion_pose_t pose;
        fable_motion_eval(p, &idle, open_end, &pose);
        if (pose.lid_left_q10 > 900U) {
            break;
        }
        open_end += MS;
    }
    const uint32_t closing = start - close_begin;
    const uint32_t opening = open_end - start;
    CHECK(closing < opening,
          "blink not fast-close/slow-open: close %u ms, open %u ms",
          closing / MS, opening / MS);
    CHECK(closing / MS < 220U, "closing too slow: %u ms", closing / MS);
    CHECK(opening / MS < 900U, "opening too slow: %u ms", opening / MS);
}

static void test_keyframe_lid_override(void)
{
    fable_keyframe_t kf = make_kf(FABLE_ACTIVITY_IDLE, 0);
    kf.eye_left_open = 0;
    kf.eye_right_open = 128;
    for (uint32_t clock = 0; clock < 30U * 1000U * MS;
         clock += 333U * MS) {
        fable_motion_pose_t pose;
        fable_motion_eval(&FABLE_PERSONA_PERKY, &kf, clock, &pose);
        CHECK(pose.lid_left_q10 == 0U, "left lid must obey keyframe");
        CHECK(pose.lid_right_q10 <= 515U,
              "right lid exceeds keyframe cap: %u", pose.lid_right_q10);
    }
}

static void test_follow_through(void)
{
    /* Thinking keeps acts/nods away so the gaze chain is isolated. */
    const fable_keyframe_t kf = make_kf(FABLE_ACTIVITY_THINKING, 0);
    const fable_persona_t *p = &FABLE_PERSONA_CURIOUS;
    const uint32_t step = 33U * MS; /* ~one 30 fps frame */
    uint32_t onsets = 0;
    uint32_t eye_leads = 0;
    fable_motion_pose_t prev;
    fable_motion_eval(p, &kf, 0, &prev);

    for (uint32_t clock = step; clock < 20U * (uint32_t)MINUTE;
         clock += step) {
        fable_motion_pose_t now;
        fable_motion_eval(p, &kf, clock, &now);
        const int32_t eye_step = now.eye_x_q2 - prev.eye_x_q2;
        const int32_t head_step = now.head_x_q2 - prev.head_x_q2;
        if (eye_step > 12 || eye_step < -12) {
            onsets++;
            if ((eye_step < 0 ? -eye_step : eye_step) >
                (head_step < 0 ? -head_step : head_step)) {
                eye_leads++;
            }
        }
        prev = now;
    }
    CHECK(onsets >= 30U, "too few saccade onsets observed: %u", onsets);
    CHECK(eye_leads * 10U >= onsets * 7U,
          "eyes do not lead the head: %u of %u", eye_leads, onsets);
}

static void test_head_tracks_gaze(void)
{
    /* At quiet fixations the head should rest near follow_pct of the
       eye offset (micro-noise only ever moves the eye layer). */
    const fable_keyframe_t kf = make_kf(FABLE_ACTIVITY_THINKING, 0);
    const fable_persona_t *p = &FABLE_PERSONA_SAGE;
    const uint32_t step = 33U * MS;
    uint32_t quiet = 0;
    uint32_t tracked = 0;
    fable_motion_pose_t window[16];
    uint32_t filled = 0;

    for (uint32_t clock = 0; clock < 10U * (uint32_t)MINUTE;
         clock += step) {
        fable_motion_pose_t pose;
        fable_motion_eval(p, &kf, clock, &pose);
        window[filled % 16U] = pose;
        filled++;
        if (filled < 16U) {
            continue;
        }
        int32_t min_eye = 32767;
        int32_t max_eye = -32768;
        for (uint32_t i = 0; i < 16U; i++) {
            if (window[i].eye_x_q2 < min_eye) {
                min_eye = window[i].eye_x_q2;
            }
            if (window[i].eye_x_q2 > max_eye) {
                max_eye = window[i].eye_x_q2;
            }
        }
        if (max_eye - min_eye > 6) {
            continue; /* not a quiet fixation */
        }
        quiet++;
        const int32_t want =
            (pose.eye_x_q2 * (int32_t)p->head_follow_pct) / 100;
        const int32_t err = pose.head_x_q2 - want;
        if (err >= -14 && err <= 14) {
            tracked++;
        }
    }
    CHECK(quiet >= 100U, "too few quiet fixations: %u", quiet);
    CHECK(tracked * 10U >= quiet * 8U,
          "head does not settle on follow share: %u of %u", tracked,
          quiet);
}

static void test_anticipation(void)
{
    const fable_keyframe_t kf = make_kf(FABLE_ACTIVITY_THINKING, 0);
    const fable_persona_t *p = &FABLE_PERSONA_CURIOUS;
    const uint32_t step = 16U * MS;
    const uint32_t antic = (uint32_t)p->anticipation_ms * MS;
    uint32_t qualifying = 0;
    uint32_t countered = 0;
    fable_motion_pose_t prev;
    fable_motion_eval(p, &kf, 0, &prev);

    for (uint32_t clock = step; clock < 30U * (uint32_t)MINUTE;
         clock += step) {
        fable_motion_pose_t now;
        fable_motion_eval(p, &kf, clock, &now);
        const int32_t eye_step = now.eye_x_q2 - prev.eye_x_q2;
        const int32_t min_q2 = ((int32_t)p->anticipation_min_px + 2) * 4;
        if (eye_step > min_q2 || eye_step < -min_q2) {
            /* Compare the head just before onset with its position one
               anticipation window earlier. */
            fable_motion_pose_t before;
            fable_motion_pose_t base;
            fable_motion_eval(p, &kf, clock - step, &before);
            fable_motion_eval(p, &kf, clock - step - antic, &base);
            const int32_t head_pre = before.head_x_q2 - base.head_x_q2;
            qualifying++;
            if ((eye_step > 0 && head_pre < 0) ||
                (eye_step < 0 && head_pre > 0)) {
                countered++;
            }
        }
        prev = now;
    }
    CHECK(qualifying >= 20U, "too few large saccades: %u", qualifying);
    CHECK(countered * 10U >= qualifying * 5U,
          "anticipation counter-move missing: %u of %u", countered,
          qualifying);
}

static void test_breathing(void)
{
    const fable_keyframe_t idle = make_kf(FABLE_ACTIVITY_IDLE, 0);
    for (uint32_t pi = 0; pi < FABLE_PERSONA_COUNT; pi++) {
        const fable_persona_t *p = fable_persona_at(pi);
        uint32_t cycles = 0;
        int above = 0;
        for (uint32_t clock = 0; clock < 5U * (uint32_t)MINUTE;
             clock += 50U * MS) {
            fable_motion_pose_t pose;
            fable_motion_eval(p, &idle, clock, &pose);
            const int now_above = pose.breath > 60U;
            if (now_above && !above) {
                cycles++;
            }
            above = now_above;
        }
        const uint32_t expected =
            (5U * 60U * 1000U) / p->breath_period_ms;
        CHECK(cycles >= expected - expected / 3U,
              "%s breathes too slowly: %u vs %u", p->slug, cycles,
              expected);
        CHECK(cycles <= expected + expected / 3U + 2U,
              "%s breathes too fast: %u vs %u", p->slug, cycles,
              expected);
    }
}

static void test_acts(void)
{
    const fable_keyframe_t busy[3] = {
        make_kf(FABLE_ACTIVITY_LISTENING, 0),
        make_kf(FABLE_ACTIVITY_THINKING, 0),
        make_kf(FABLE_ACTIVITY_SPEAKING, 1),
    };
    const fable_keyframe_t idle = make_kf(FABLE_ACTIVITY_IDLE, 0);

    for (uint32_t pi = 0; pi < FABLE_PERSONA_COUNT; pi++) {
        const fable_persona_t *p = fable_persona_at(pi);
        uint32_t idle_acts = 0;
        for (uint32_t clock = 0; clock < 20U * (uint32_t)MINUTE;
             clock += 100U * MS) {
            fable_motion_pose_t pose;
            for (size_t fi = 0; fi < 3U; fi++) {
                fable_motion_eval(p, &busy[fi], clock, &pose);
                CHECK(pose.act == FABLE_ACT_NONE,
                      "%s staged act %u while busy", p->slug, pose.act);
            }
            fable_motion_eval(p, &idle, clock, &pose);
            if (pose.act != FABLE_ACT_NONE) {
                idle_acts++;
                CHECK((p->act_mask & FABLE_ACT_BIT(pose.act)) != 0U,
                      "%s staged act %u outside its mask", p->slug,
                      pose.act);
            }
        }
        CHECK(idle_acts >= 5U, "%s never fidgets: %u samples", p->slug,
              idle_acts);
    }
}

static void test_speaking_gaze_differs(void)
{
    /* Speaking keeps gaze busier / more averted than listening. */
    const fable_keyframe_t listen = make_kf(FABLE_ACTIVITY_LISTENING, 0);
    const fable_keyframe_t speak = make_kf(FABLE_ACTIVITY_SPEAKING, 1);
    const fable_persona_t *p = &FABLE_PERSONA_SAGE;
    int64_t sum_listen = 0;
    int64_t sum_speak = 0;
    uint32_t n = 0;
    for (uint32_t clock = 0; clock < 10U * (uint32_t)MINUTE;
         clock += 66U * MS) {
        fable_motion_pose_t pose;
        fable_motion_eval(p, &listen, clock, &pose);
        sum_listen += pose.eye_x_q2 < 0 ? -pose.eye_x_q2 : pose.eye_x_q2;
        fable_motion_eval(p, &speak, clock, &pose);
        sum_speak += pose.eye_x_q2 < 0 ? -pose.eye_x_q2 : pose.eye_x_q2;
        n++;
    }
    CHECK(n > 0U, "no samples");
    CHECK(sum_speak > sum_listen,
          "speaking gaze (%lld) not busier than listening (%lld)",
          (long long)sum_speak, (long long)sum_listen);
}

int main(void)
{
    if (!fable_shift_is_arithmetic()) {
        printf("FAIL: platform lacks arithmetic right shift\n");
        return 1;
    }
    test_determinism();
    test_bounds();
    test_blink_rate();
    test_blink_shape();
    test_keyframe_lid_override();
    test_follow_through();
    test_head_tracks_gaze();
    test_anticipation();
    test_breathing();
    test_acts();
    test_speaking_gaze_differs();
    return fable_test_finish("test_motion");
}
