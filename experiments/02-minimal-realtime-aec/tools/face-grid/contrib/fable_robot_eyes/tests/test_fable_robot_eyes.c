/*
 * Test suite for the fable_robot_eyes contribution.
 *
 * Beyond API/ABI checks, most tests assert on *behavior*: blink rate and
 * kinematic asymmetry, saccade-vs-fixation structure, social-gaze
 * differences between activities, lid anticipation, drowsiness, and the
 * keyframe contract. Everything runs on the public API only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../fable_robot_eyes.h"

static int g_failures;
static int g_checks;

#define CHECK(cond, ...) \
    do { \
        ++g_checks; \
        if (!(cond)) { \
            ++g_failures; \
            printf("FAIL %s:%d: ", __func__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static fre_keyframe_t neutral_kf(uint8_t expression)
{
    fre_keyframe_t kf;
    memset(&kf, 0, sizeof kf);
    kf.mouth_width = 128;
    kf.mouth_round = 128;
    kf.eye_left_open = 255;
    kf.eye_right_open = 255;
    kf.expression = expression;
    if (expression == FRE_ACTIVITY_SPEAKING) {
        kf.flags = FRE_KEYFRAME_FLAG_SPEAKING;
        kf.mouth_open = 120;
    }
    return kf;
}

static uint32_t ms_to_clock(uint32_t ms)
{
    return ms * 16U;
}

/* ------------------------------------------------------------------ */

static void test_api_contract(void)
{
    CHECK(fre_profile_count() == FRE_PROFILE_COUNT, "profile count");
    fre_keyframe_t kf = neutral_kf(0);
    static uint16_t buf[FRE_FRAME_PIXEL_COUNT];
    CHECK(!fre_render_frame(FRE_PROFILE_VECTOR_ROUNDED, NULL, 0, buf,
        FRE_FRAME_PIXEL_COUNT), "NULL keyframe rejected");
    CHECK(!fre_render_frame(FRE_PROFILE_VECTOR_ROUNDED, &kf, 0, NULL,
        FRE_FRAME_PIXEL_COUNT), "NULL buffer rejected");
    CHECK(!fre_render_frame(FRE_PROFILE_VECTOR_ROUNDED, &kf, 0, buf,
        FRE_FRAME_PIXEL_COUNT - 1), "short buffer rejected");
    CHECK(!fre_render_frame(FRE_PROFILE_COUNT, &kf, 0, buf,
        FRE_FRAME_PIXEL_COUNT), "invalid profile rejected");
    for (size_t p = 0; p < fre_profile_count(); ++p) {
        fre_profile_info_t info;
        CHECK(fre_profile_slug((fre_profile_t)p) != NULL, "slug %zu", p);
        CHECK(fre_profile_name((fre_profile_t)p) != NULL, "name %zu", p);
        CHECK(fre_profile_family_name((fre_profile_t)p) != NULL,
            "family %zu", p);
        CHECK(fre_profile_info((fre_profile_t)p, &info), "info %zu", p);
        CHECK(info.width == FRE_FRAME_WIDTH &&
            info.height == FRE_FRAME_HEIGHT, "dims %zu", p);
        CHECK(info.framebuffer_bytes == FRE_FRAME_BYTES, "bytes %zu", p);
        CHECK((info.flags & FRE_FLAG_IDLE_MOTION) != 0,
            "idle motion advertised %zu", p);
        CHECK(info.family == FRE_FAMILY_ROBOT ||
            info.family == FRE_FAMILY_EYES, "family id %zu", p);
    }
    /* Slugs are unique. */
    for (size_t a = 0; a < fre_profile_count(); ++a) {
        for (size_t b = a + 1; b < fre_profile_count(); ++b) {
            CHECK(strcmp(fre_profile_slug((fre_profile_t)a),
                fre_profile_slug((fre_profile_t)b)) != 0,
                "slug collision %zu/%zu", a, b);
        }
    }
}

static void test_buffer_canaries(void)
{
    enum { GUARD = 512 };
    static uint16_t arena[GUARD + FRE_FRAME_PIXEL_COUNT + GUARD];
    fre_keyframe_t kf = neutral_kf(0);
    for (size_t p = 0; p < fre_profile_count(); ++p) {
        memset(arena, 0xA5, sizeof arena);
        for (uint32_t i = 0; i < 5; ++i) {
            CHECK(fre_render_frame((fre_profile_t)p, &kf,
                ms_to_clock(i * 777U), arena + GUARD,
                FRE_FRAME_PIXEL_COUNT), "render %zu/%u", p, i);
        }
        bool clean = true;
        for (size_t i = 0; i < GUARD; ++i) {
            if (arena[i] != 0xA5A5 ||
                arena[GUARD + FRE_FRAME_PIXEL_COUNT + i] != 0xA5A5) {
                clean = false;
            }
        }
        CHECK(clean, "canary intact for profile %zu", p);
    }
}

static void test_determinism(void)
{
    static uint16_t a[FRE_FRAME_PIXEL_COUNT];
    static uint16_t b[FRE_FRAME_PIXEL_COUNT];
    fre_keyframe_t kf = neutral_kf(3);
    kf.look_x = 40;
    kf.brow = -30;
    for (size_t p = 0; p < fre_profile_count(); ++p) {
        fre_render_frame((fre_profile_t)p, &kf, ms_to_clock(12345),
            a, FRE_FRAME_PIXEL_COUNT);
        fre_render_frame((fre_profile_t)p, &kf, ms_to_clock(12345),
            b, FRE_FRAME_PIXEL_COUNT);
        CHECK(memcmp(a, b, sizeof a) == 0, "frame determinism %zu", p);
    }
    fre_rig_t r1, r2;
    fre_behavior_solve(FRE_PROFILE_SACCADE_LAB, &kf,
        ms_to_clock(98765), &r1);
    fre_behavior_solve(FRE_PROFILE_SACCADE_LAB, &kf,
        ms_to_clock(98765), &r2);
    CHECK(memcmp(&r1, &r2, sizeof r1) == 0, "rig determinism");
}

/* Count lit (non-background) pixels inside the upper 2/3 of the frame. */
static int lit_pixels(const uint16_t *buf, uint16_t bg)
{
    int n = 0;
    for (int y = 0; y < FRE_FRAME_HEIGHT * 2 / 3; ++y) {
        for (int x = 0; x < FRE_FRAME_WIDTH; ++x) {
            if (buf[y * FRE_FRAME_WIDTH + x] != bg) {
                ++n;
            }
        }
    }
    return n;
}

static int lit_centroid_x(const uint16_t *buf, uint16_t bg)
{
    int64_t sum = 0;
    int n = 0;
    for (int y = 0; y < FRE_FRAME_HEIGHT; ++y) {
        for (int x = 0; x < FRE_FRAME_WIDTH; ++x) {
            if (buf[y * FRE_FRAME_WIDTH + x] != bg) {
                sum += x;
                ++n;
            }
        }
    }
    return n > 0 ? (int)(sum / n) : 0;
}

static void test_keyframe_response(void)
{
    static uint16_t buf[FRE_FRAME_PIXEL_COUNT];
    /* Eye openness valve. */
    fre_keyframe_t open_kf = neutral_kf(0);
    fre_keyframe_t shut_kf = neutral_kf(0);
    shut_kf.eye_left_open = 0;
    shut_kf.eye_right_open = 0;
    /* Pick a quiet time (no blink) by scanning the rig. */
    uint32_t quiet_ms = 0;
    for (uint32_t ms = 500; ms < 20000; ms += 100) {
        fre_rig_t rig;
        fre_behavior_solve(FRE_PROFILE_VECTOR_ROUNDED, &open_kf,
            ms_to_clock(ms), &rig);
        if (rig.openness_q8[0] > 240 && rig.openness_q8[1] > 240) {
            quiet_ms = ms;
            break;
        }
    }
    CHECK(quiet_ms != 0, "found a non-blink instant");
    fre_render_frame(FRE_PROFILE_VECTOR_ROUNDED, &open_kf,
        ms_to_clock(quiet_ms), buf, FRE_FRAME_PIXEL_COUNT);
    int lit_open = lit_pixels(buf, 0x0000);
    fre_render_frame(FRE_PROFILE_VECTOR_ROUNDED, &shut_kf,
        ms_to_clock(quiet_ms), buf, FRE_FRAME_PIXEL_COUNT);
    int lit_shut = lit_pixels(buf, 0x0000);
    CHECK(lit_shut < lit_open / 3,
        "closed eyes shrink lit area (%d -> %d)", lit_open, lit_shut);

    /* Host gaze shifts the face. */
    fre_keyframe_t left_kf = neutral_kf(1);
    fre_keyframe_t right_kf = neutral_kf(1);
    left_kf.look_x = -110;
    right_kf.look_x = 110;
    fre_render_frame(FRE_PROFILE_VECTOR_ROUNDED, &left_kf,
        ms_to_clock(quiet_ms), buf, FRE_FRAME_PIXEL_COUNT);
    int cx_left = lit_centroid_x(buf, 0x0000);
    fre_render_frame(FRE_PROFILE_VECTOR_ROUNDED, &right_kf,
        ms_to_clock(quiet_ms), buf, FRE_FRAME_PIXEL_COUNT);
    int cx_right = lit_centroid_x(buf, 0x0000);
    CHECK(cx_right > cx_left + 8,
        "look_x moves centroid (%d vs %d)", cx_left, cx_right);

    /* BLINKING flag caps aperture. */
    fre_keyframe_t blink_kf = neutral_kf(0);
    blink_kf.flags |= FRE_KEYFRAME_FLAG_BLINKING;
    fre_rig_t rig;
    fre_behavior_solve(FRE_PROFILE_VECTOR_ROUNDED, &blink_kf,
        ms_to_clock(quiet_ms), &rig);
    CHECK(rig.openness_q8[0] <= 48 && rig.openness_q8[1] <= 48,
        "blink flag caps openness (%d/%d)",
        (int)rig.openness_q8[0], (int)rig.openness_q8[1]);

    /* Brow byte drives the rig. */
    fre_keyframe_t brow_up = neutral_kf(0);
    fre_keyframe_t brow_dn = neutral_kf(0);
    brow_up.brow = 100;
    brow_dn.brow = -100;
    fre_rig_t rig_up, rig_dn;
    fre_behavior_solve(FRE_PROFILE_BROW_DIALOGUE, &brow_up,
        ms_to_clock(quiet_ms), &rig_up);
    fre_behavior_solve(FRE_PROFILE_BROW_DIALOGUE, &brow_dn,
        ms_to_clock(quiet_ms), &rig_dn);
    CHECK(rig_up.brow_raise_q8[0] > rig_dn.brow_raise_q8[0] + 100,
        "brow byte respected (%d vs %d)",
        (int)rig_up.brow_raise_q8[0], (int)rig_dn.brow_raise_q8[0]);
}

/*
 * Blink census over a window: events, their closing and reopening
 * durations, and the deepest closure.
 */
typedef struct {
    int count;
    int full_closures;
    int asym_ok; /* reopen took longer than close */
    int min_duration_ms;
    int max_duration_ms;
} blink_census_t;

static void census_blinks(
    fre_profile_t profile,
    const fre_keyframe_t *kf,
    uint32_t t0_ms,
    uint32_t t1_ms,
    blink_census_t *out)
{
    const int STEP = 4;
    out->count = 0;
    out->full_closures = 0;
    out->asym_ok = 0;
    out->min_duration_ms = 1 << 30;
    out->max_duration_ms = 0;
    bool in_blink = false;
    uint32_t start = 0, below_10_start = 0, below_10_end = 0;
    int32_t deepest = 256;
    for (uint32_t ms = t0_ms; ms < t1_ms; ms += STEP) {
        fre_rig_t rig;
        fre_behavior_solve(profile, kf, ms_to_clock(ms), &rig);
        int32_t a = rig.openness_q8[0];
        if (!in_blink && a < 190) {
            in_blink = true;
            start = ms;
            deepest = a;
            below_10_start = 0;
            below_10_end = 0;
        } else if (in_blink) {
            if (a < deepest) {
                deepest = a;
            }
            if (a < 26 && below_10_start == 0) {
                below_10_start = ms;
            }
            if (a < 26) {
                below_10_end = ms;
            }
            if (a > 230) {
                int dur = (int)(ms - start);
                ++out->count;
                if (deepest < 26) {
                    ++out->full_closures;
                }
                if (below_10_start != 0) {
                    int closing = (int)(below_10_start - start);
                    int opening = (int)(ms - below_10_end);
                    if (opening > closing) {
                        ++out->asym_ok;
                    }
                }
                if (dur < out->min_duration_ms) {
                    out->min_duration_ms = dur;
                }
                if (dur > out->max_duration_ms) {
                    out->max_duration_ms = dur;
                }
                in_blink = false;
            }
        }
    }
}

static void test_blink_kinematics(void)
{
    fre_keyframe_t kf = neutral_kf(0);
    blink_census_t c;
    census_blinks(FRE_PROFILE_VECTOR_ROUNDED, &kf, 0, 120000, &c);
    /* Idle baseline ~17/min; the scheduler must land in a plausible
     * band over two minutes (Bentivoglio 1997 range across subjects). */
    CHECK(c.count >= 16 && c.count <= 90,
        "idle blink count over 2 min: %d", c.count);
    /* Trutoiu 2011: fully closing blinks read most natural; most of
     * ours close fully (a doublet's second dip may not). */
    CHECK(c.full_closures * 10 >= c.count * 6,
        "most blinks close fully (%d of %d)", c.full_closures, c.count);
    /* VanderWerf 2003: reopening lasts ~2-3x the down-phase. */
    CHECK(c.asym_ok * 10 >= c.count * 7,
        "reopen slower than close (%d of %d)", c.asym_ok, c.count);
    /* A census "episode" can chain a doublet or a gaze-evoked blink
     * onto a spontaneous one, so the upper bound covers two blinks. */
    CHECK(c.min_duration_ms >= 80 && c.max_duration_ms <= 1400,
        "blink episode durations sane (%d..%d ms)",
        c.min_duration_ms, c.max_duration_ms);
}

static void test_blink_rate_by_activity(void)
{
    fre_keyframe_t speaking = neutral_kf(3);
    fre_keyframe_t listening = neutral_kf(1);
    blink_census_t cs, cl;
    census_blinks(FRE_PROFILE_VECTOR_ROUNDED, &speaking, 0, 120000, &cs);
    census_blinks(FRE_PROFILE_VECTOR_ROUNDED, &listening, 0, 120000, &cl);
    /* Speakers blink far more than listeners (Bailly et al.). */
    CHECK(cs.count > cl.count,
        "speaking blinks (%d) > listening blinks (%d)",
        cs.count, cl.count);
}

/*
 * Gaze structure: fixations with fast transitions, not smooth drift.
 * Samples gaze every 4 ms and classifies movement speed.
 */
static void test_saccade_structure(void)
{
    fre_keyframe_t kf = neutral_kf(0);
    const int STEP = 4;
    int fast_steps = 0, slow_steps = 0, still_steps = 0;
    int32_t prev_x = 0, prev_y = 0;
    bool have_prev = false;
    int32_t min_x = 999, max_x = -999;
    for (uint32_t ms = 0; ms < 120000; ms += STEP) {
        fre_rig_t rig;
        fre_behavior_solve(FRE_PROFILE_VECTOR_ROUNDED, &kf,
            ms_to_clock(ms), &rig);
        CHECK(rig.gaze_x_q8 >= -256 && rig.gaze_x_q8 <= 256 &&
            rig.gaze_y_q8 >= -256 && rig.gaze_y_q8 <= 256,
            "gaze clamped at %u", ms);
        if (rig.gaze_x_q8 < min_x) {
            min_x = rig.gaze_x_q8;
        }
        if (rig.gaze_x_q8 > max_x) {
            max_x = rig.gaze_x_q8;
        }
        if (have_prev) {
            int32_t v = (int32_t)(labs(rig.gaze_x_q8 - prev_x) +
                labs(rig.gaze_y_q8 - prev_y));
            if (v > 6) {
                ++fast_steps;
            } else if (v > 1) {
                ++slow_steps;
            } else {
                ++still_steps;
            }
        }
        prev_x = rig.gaze_x_q8;
        prev_y = rig.gaze_y_q8;
        have_prev = true;
    }
    /* The eye must actually travel... */
    CHECK(max_x - min_x > 120, "gaze range used (%d..%d)", min_x, max_x);
    /* ...but overwhelmingly by holding fixations punctuated by fast
     * saccades: still time dominates, fast steps exist but are rare. */
    int total = fast_steps + slow_steps + still_steps;
    CHECK(still_steps * 10 > total * 5,
        "fixation-dominated timeline (%d/%d still)", still_steps, total);
    CHECK(fast_steps > 0, "saccadic jumps present");
    CHECK(fast_steps * 100 < total * 8,
        "saccades are brief (%d/%d fast)", fast_steps, total);
}

static void test_social_gaze_by_activity(void)
{
    fre_keyframe_t idle = neutral_kf(0);
    fre_keyframe_t listening = neutral_kf(1);
    fre_keyframe_t thinking = neutral_kf(2);
    fre_keyframe_t speaking = neutral_kf(3);
    int64_t idle_dev = 0, listen_dev = 0;
    int64_t think_y = 0, idle_y = 0;
    int listen_avert = 0, speak_avert = 0;
    const int N = 24000; /* 4 ms steps over 96 s */
    for (int i = 0; i < N; ++i) {
        uint32_t clk = ms_to_clock((uint32_t)i * 4U);
        fre_rig_t r;
        fre_behavior_solve(FRE_PROFILE_VECTOR_ROUNDED, &idle, clk, &r);
        idle_dev += labs(r.gaze_x_q8) + labs(r.gaze_y_q8);
        idle_y += r.gaze_y_q8;
        fre_behavior_solve(FRE_PROFILE_VECTOR_ROUNDED, &listening, clk,
            &r);
        listen_dev += labs(r.gaze_x_q8) + labs(r.gaze_y_q8);
        if (labs(r.gaze_x_q8) + labs(r.gaze_y_q8) > 70) {
            ++listen_avert;
        }
        fre_behavior_solve(FRE_PROFILE_VECTOR_ROUNDED, &thinking, clk,
            &r);
        think_y += r.gaze_y_q8;
        fre_behavior_solve(FRE_PROFILE_VECTOR_ROUNDED, &speaking, clk,
            &r);
        if (labs(r.gaze_x_q8) + labs(r.gaze_y_q8) > 70) {
            ++speak_avert;
        }
    }
    /* Listeners hold gaze: mean |gaze| well below idle roaming. */
    CHECK(listen_dev * 10 < idle_dev * 6,
        "listening steadier than idle (%lld vs %lld)",
        (long long)listen_dev, (long long)idle_dev);
    /* Thinking looks up on average (screen up is negative). */
    CHECK(think_y < idle_y,
        "thinking gaze biased upward (%lld vs %lld)",
        (long long)think_y, (long long)idle_y);
    /* Speakers avert more than listeners (Kendon; Eyes Alive). */
    CHECK(speak_avert > listen_avert,
        "speaking averts more (%d vs %d)", speak_avert, listen_avert);
}

static void test_lid_anticipation(void)
{
    fre_keyframe_t kf = neutral_kf(0);
    /* The lid gaze channel must predict the eye gaze channel: comparing
     * lid_gaze_y(t) with gaze_y(t + lead) should beat comparing it with
     * gaze_y(t) over a long window containing saccades. */
    const int LEAD = 110; /* lid_anticipation profile lookahead */
    int64_t err_lead = 0, err_now = 0;
    for (uint32_t ms = 0; ms < 60000; ms += 16) {
        fre_rig_t now, ahead;
        fre_behavior_solve(FRE_PROFILE_LID_ANTICIPATION, &kf,
            ms_to_clock(ms), &now);
        fre_behavior_solve(FRE_PROFILE_LID_ANTICIPATION, &kf,
            ms_to_clock(ms + LEAD), &ahead);
        err_lead += labs(now.lid_gaze_y_q8 - ahead.gaze_y_q8);
        err_now += labs(now.lid_gaze_y_q8 - now.gaze_y_q8);
    }
    CHECK(err_lead < err_now,
        "lids lead the gaze (err_lead=%lld err_now=%lld)",
        (long long)err_lead, (long long)err_now);
}

static void test_drowsiness(void)
{
    fre_keyframe_t idle = neutral_kf(0);
    fre_keyframe_t listening = neutral_kf(1);
    /* Sample sleep phase (30-42 s of the 45 s doze cycle). */
    int64_t asleep_open = 0, alert_open = 0, listen_open = 0;
    int n = 0;
    for (uint32_t ms = 33000; ms < 40000; ms += 50) {
        fre_rig_t r;
        fre_behavior_solve(FRE_PROFILE_SLEEP_WAKE, &idle,
            ms_to_clock(ms), &r);
        asleep_open += r.openness_q8[0];
        fre_behavior_solve(FRE_PROFILE_SLEEP_WAKE, &listening,
            ms_to_clock(ms), &r);
        listen_open += r.openness_q8[0];
        fre_behavior_solve(FRE_PROFILE_SLEEP_WAKE, &idle,
            ms_to_clock(ms - 30000), &r);
        alert_open += r.openness_q8[0];
        ++n;
    }
    CHECK(asleep_open / n < 40,
        "asleep at 33-40 s of doze cycle (avg %lld)",
        (long long)(asleep_open / n));
    CHECK(alert_open / n > 150,
        "alert early in doze cycle (avg %lld)",
        (long long)(alert_open / n));
    CHECK(listen_open / n > 150,
        "listening suppresses dozing (avg %lld)",
        (long long)(listen_open / n));
    /* Other profiles never doze. */
    fre_rig_t r;
    fre_behavior_solve(FRE_PROFILE_VECTOR_ROUNDED, &idle,
        ms_to_clock(35000), &r);
    CHECK(r.openness_q8[0] > 100 || r.openness_q8[1] > 100,
        "non-drowsy profile stays awake");
}

static void test_breathing(void)
{
    fre_keyframe_t kf = neutral_kf(0);
    int32_t min_b = 1 << 30, max_b = -(1 << 30);
    int crossings = 0;
    int last_sign = 0;
    for (uint32_t ms = 0; ms < 20000; ms += 20) {
        fre_rig_t r;
        fre_behavior_solve(FRE_PROFILE_M5_AVATAR_CLASSIC, &kf,
            ms_to_clock(ms), &r);
        if (r.breath_y_q8 < min_b) {
            min_b = r.breath_y_q8;
        }
        if (r.breath_y_q8 > max_b) {
            max_b = r.breath_y_q8;
        }
        int sign = (r.breath_y_q8 > 0) - (r.breath_y_q8 < 0);
        if (sign != 0) {
            if (last_sign != 0 && sign != last_sign) {
                ++crossings;
            }
            last_sign = sign;
        }
    }
    /* Idle period 4.6 s over 20 s => ~8-9 zero crossings. */
    CHECK(crossings >= 6 && crossings <= 12,
        "breathing period plausible (%d crossings)", crossings);
    CHECK(max_b > 150 && min_b < -150 && max_b < 900 && -min_b < 900,
        "breathing amplitude sane (%d..%d)", min_b, max_b);
}

static void test_idle_acts_variety(void)
{
    fre_keyframe_t kf = neutral_kf(0);
    uint32_t seen_mask = 0;
    for (uint32_t ms = 0; ms < 15U * 60U * 1000U; ms += 40) {
        fre_rig_t r;
        fre_behavior_solve(FRE_PROFILE_BROW_DIALOGUE, &kf,
            ms_to_clock(ms), &r);
        seen_mask |= 1U << r.act_id;
    }
    int distinct = 0;
    for (int i = 1; i < 16; ++i) {
        if ((seen_mask & (1U << i)) != 0U) {
            ++distinct;
        }
    }
    CHECK(distinct >= 4,
        "idle repertoire runs several act kinds (%d, mask %x)",
        distinct, seen_mask);
}

static void test_profiles_differ(void)
{
    static uint16_t a[FRE_FRAME_PIXEL_COUNT];
    static uint16_t b[FRE_FRAME_PIXEL_COUNT];
    fre_keyframe_t kf = neutral_kf(0);
    for (size_t p = 1; p < fre_profile_count(); ++p) {
        fre_render_frame((fre_profile_t)(p - 1), &kf, ms_to_clock(900),
            a, FRE_FRAME_PIXEL_COUNT);
        fre_render_frame((fre_profile_t)p, &kf, ms_to_clock(900),
            b, FRE_FRAME_PIXEL_COUNT);
        CHECK(memcmp(a, b, sizeof a) != 0,
            "profiles %zu and %zu differ", p - 1, p);
    }
    /* And every profile draws something. */
    for (size_t p = 0; p < fre_profile_count(); ++p) {
        fre_render_frame((fre_profile_t)p, &kf, ms_to_clock(900),
            a, FRE_FRAME_PIXEL_COUNT);
        uint16_t first = a[0];
        bool any = false;
        for (int i = 0; i < FRE_FRAME_PIXEL_COUNT; ++i) {
            if (a[i] != first) {
                any = true;
                break;
            }
        }
        CHECK(any, "profile %zu draws content", p);
    }
}

/*
 * Golden hashes: FNV-1a across a matrix of profiles, activities, and
 * timestamps. Regenerate with `make dump && ./build/fre_dump hash`
 * after an intentional visual change and paste the values here.
 */
static uint32_t fnv1a(uint32_t h, const unsigned char *d, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        h ^= d[i];
        h *= 16777619U;
    }
    return h;
}

static void test_golden_hashes(void)
{
    static const uint32_t times_ms[] = {
        0, 137, 1500, 3210, 9999, 45123, 120000,
    };
    static uint16_t buf[FRE_FRAME_PIXEL_COUNT];
    /* One hash per profile covering all four activities. */
    static const uint32_t expected[FRE_PROFILE_COUNT] = {
        0x0003ee07, 0x60ad0dfa, 0x0f5855d8, 0x46d65848,
        0x4fa74acd, 0x70e7098f, 0x1a513e6e, 0xa58a3676,
        0x890602fb, 0x71475424, 0x5027ceb1, 0x5864a551,
        0x6d7457bd, 0x153bf1e4, 0x02ae242d, 0x7c2f4aec,
    };
    bool golden_ready = false;
    for (size_t p = 0; p < FRE_PROFILE_COUNT; ++p) {
        if (expected[p] != 0U) {
            golden_ready = true;
        }
    }
    for (size_t p = 0; p < fre_profile_count(); ++p) {
        uint32_t h = 2166136261U;
        for (int expr = 0; expr < 4; ++expr) {
            fre_keyframe_t kf = neutral_kf((uint8_t)expr);
            for (size_t t = 0;
                 t < sizeof times_ms / sizeof times_ms[0]; ++t) {
                fre_render_frame((fre_profile_t)p, &kf,
                    ms_to_clock(times_ms[t]), buf,
                    FRE_FRAME_PIXEL_COUNT);
                h = fnv1a(h, (const unsigned char *)buf, sizeof buf);
            }
        }
        if (golden_ready) {
            CHECK(h == expected[p],
                "golden hash profile %zu (got %08x want %08x)",
                p, h, expected[p]);
        } else {
            printf("golden %-24s %08x\n",
                fre_profile_slug((fre_profile_t)p), h);
        }
    }
    if (!golden_ready) {
        printf("NOTE: golden hashes not yet frozen; paste the values "
               "above into test_golden_hashes.\n");
    }
}

int main(void)
{
    test_api_contract();
    test_buffer_canaries();
    test_determinism();
    test_keyframe_response();
    test_blink_kinematics();
    test_blink_rate_by_activity();
    test_saccade_structure();
    test_social_gaze_by_activity();
    test_lid_anticipation();
    test_drowsiness();
    test_breathing();
    test_idle_acts_variety();
    test_profiles_differ();
    test_golden_hashes();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
