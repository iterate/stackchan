#include "../src/fable_ease.h"

#include "test_support.h"

typedef int32_t (*ease_fn_t)(int32_t);

static void check_endpoints(const char *name, ease_fn_t fn)
{
    CHECK(fn(0) == 0, "%s(0) = %d, want 0", name, fn(0));
    CHECK(fn(FABLE_ONE) == FABLE_ONE, "%s(1) = %d, want %d", name,
          fn(FABLE_ONE), FABLE_ONE);
    /* Out-of-range phases clamp instead of extrapolating. */
    CHECK(fn(-500) == fn(0), "%s clamps below", name);
    CHECK(fn(FABLE_ONE + 500) == fn(FABLE_ONE), "%s clamps above", name);
}

static void check_monotone(const char *name, ease_fn_t fn)
{
    int32_t prev = fn(0);
    for (int32_t t = 1; t <= FABLE_ONE; t++) {
        const int32_t v = fn(t);
        CHECK(v >= prev, "%s not monotone at t=%d (%d < %d)", name, t, v,
              prev);
        prev = v;
    }
}

static void check_bounded(const char *name, ease_fn_t fn, int32_t lo,
                          int32_t hi)
{
    for (int32_t t = 0; t <= FABLE_ONE; t++) {
        const int32_t v = fn(t);
        CHECK(v >= lo && v <= hi, "%s(%d) = %d outside [%d, %d]", name,
              t, v, lo, hi);
    }
}

static int32_t settle_6(int32_t t)
{
    return fable_settle(t, 6);
}

static void test_eases(void)
{
    check_endpoints("linear", fable_ease_linear);
    check_endpoints("in_quad", fable_ease_in_quad);
    check_endpoints("out_quad", fable_ease_out_quad);
    check_endpoints("in_cubic", fable_ease_in_cubic);
    check_endpoints("out_cubic", fable_ease_out_cubic);
    check_endpoints("smooth", fable_ease_smooth);
    check_endpoints("smoother", fable_ease_smoother);
    check_endpoints("out_back", fable_ease_out_back);
    check_endpoints("in_back", fable_ease_in_back);
    check_endpoints("settle6", settle_6);

    check_monotone("linear", fable_ease_linear);
    check_monotone("in_quad", fable_ease_in_quad);
    check_monotone("out_quad", fable_ease_out_quad);
    check_monotone("in_cubic", fable_ease_in_cubic);
    check_monotone("out_cubic", fable_ease_out_cubic);
    check_monotone("smooth", fable_ease_smooth);
    check_monotone("smoother", fable_ease_smoother);

    /* Smoothstep symmetry about the midpoint. */
    for (int32_t t = 0; t <= FABLE_ONE; t++) {
        const int32_t sum =
            fable_ease_smooth(t) + fable_ease_smooth(FABLE_ONE - t);
        CHECK(sum >= FABLE_ONE - 2 && sum <= FABLE_ONE + 2,
              "smooth symmetry at t=%d: sum=%d", t, sum);
    }

    /* Overshoot family: out_back peaks ~10%% above one and comes home. */
    int32_t peak = 0;
    for (int32_t t = 0; t <= FABLE_ONE; t++) {
        peak = peak > fable_ease_out_back(t) ? peak
                                             : fable_ease_out_back(t);
    }
    CHECK(peak > FABLE_ONE + 60 && peak < FABLE_ONE + 140,
          "out_back peak %d not near 1.10", peak);
    check_bounded("out_back", fable_ease_out_back, -8, FABLE_ONE + 140);

    int32_t dip = FABLE_ONE;
    for (int32_t t = 0; t <= FABLE_ONE; t++) {
        dip = dip < fable_ease_in_back(t) ? dip : fable_ease_in_back(t);
    }
    CHECK(dip < -60 && dip > -140, "in_back dip %d not near -0.10", dip);

    /* Damped settle: bounded, oscillates, lands exactly on one. */
    check_bounded("settle6", settle_6, -FABLE_ONE / 4,
                  FABLE_ONE + FABLE_ONE / 4);
    int32_t above = 0;
    for (int32_t t = 0; t <= FABLE_ONE; t++) {
        if (settle_6(t) > FABLE_ONE + 8) {
            above++;
        }
    }
    CHECK(above > 10, "settle6 never overshoots (above=%d)", above);

    /* Arc: zero at the ends, peak at the middle. */
    CHECK(fable_arc(0) == 0, "arc(0)");
    CHECK(fable_arc(FABLE_ONE) == 0, "arc(1)");
    CHECK(fable_arc(FABLE_ONE / 2) == FABLE_ONE, "arc(mid) = %d",
          fable_arc(FABLE_ONE / 2));
}

static void test_trig(void)
{
    CHECK(fable_cos_turn(0) == FABLE_ONE, "cos(0)");
    CHECK(fable_cos_turn(FABLE_TURN / 4) == 0, "cos(quarter)");
    CHECK(fable_cos_turn(FABLE_TURN / 2) == -FABLE_ONE, "cos(half)");
    CHECK(fable_cos_turn(3 * FABLE_TURN / 4) == 0, "cos(3/4)");
    CHECK(fable_sin_turn(FABLE_TURN / 4) == FABLE_ONE, "sin(quarter)");

    for (uint32_t a = 0; a < (uint32_t)FABLE_TURN; a++) {
        const int32_t c = fable_cos_turn(a);
        const int32_t s = fable_sin_turn(a);
        CHECK(c >= -FABLE_ONE && c <= FABLE_ONE, "cos bounds at %u", a);
        CHECK(s >= -FABLE_ONE && s <= FABLE_ONE, "sin bounds at %u", a);
        const int64_t norm = (int64_t)c * c + (int64_t)s * s;
        const int64_t unit = (int64_t)FABLE_ONE * FABLE_ONE;
        CHECK(norm > unit - unit / 16 && norm < unit + unit / 16,
              "cos^2+sin^2 at %u = %lld", a, (long long)norm);
        /* Wrap-around consistency. */
        CHECK(fable_cos_turn(a + (uint32_t)FABLE_TURN) == c,
              "cos wrap at %u", a);
    }
}

static void test_isqrt(void)
{
    for (uint32_t v = 0; v < 70000U; v++) {
        const uint32_t r = fable_isqrt(v);
        CHECK(r * r <= v, "isqrt(%u) = %u too big", v, r);
        CHECK((uint64_t)(r + 1U) * (r + 1U) > v, "isqrt(%u) = %u too small",
              v, r);
    }
    const uint32_t big[] = { 1U << 30, 0x7fffffffU, 0xffffffffU,
                             3037000499U };
    for (size_t i = 0; i < sizeof(big) / sizeof(big[0]); i++) {
        const uint32_t v = big[i];
        const uint32_t r = fable_isqrt(v);
        CHECK((uint64_t)r * r <= v, "isqrt big %u", v);
        CHECK(((uint64_t)r + 1U) * ((uint64_t)r + 1U) > v,
              "isqrt big small %u", v);
    }
}

static void test_noise(void)
{
    int32_t min = FABLE_ONE;
    int32_t max = -FABLE_ONE;
    int32_t prev = fable_vnoise(0, 16000, 7);
    for (uint32_t t = 1; t < 600000U; t += 7U) {
        const int32_t n = fable_vnoise(t, 16000, 7);
        CHECK(n >= -FABLE_ONE && n < FABLE_ONE, "vnoise bounds %d", n);
        const int32_t step = n - prev;
        CHECK(step > -40 && step < 40,
              "vnoise discontinuity %d at t=%u", step, t);
        prev = n;
        if (n < min) {
            min = n;
        }
        if (n > max) {
            max = n;
        }
    }
    CHECK(min < -256 && max > 256,
          "vnoise range too narrow [%d, %d]", min, max);
    CHECK(fable_vnoise(123456, 16000, 7) ==
              fable_vnoise(123456, 16000, 7),
          "vnoise deterministic");
    CHECK(fable_vnoise(123456, 16000, 7) !=
              fable_vnoise(123456, 16000, 8),
          "vnoise seeds decorrelate");
    CHECK(fable_vnoise(500, 0, 7) == 0, "vnoise zero period");
}

static void test_schedule(void)
{
    const uint32_t slot_len = 64000;
    const uint32_t duration = 4000;
    for (uint32_t clock = 0; clock < 40U * slot_len; clock += 997U) {
        fable_event_t ev;
        fable_schedule(clock, slot_len, duration, 42, &ev);
        CHECK(ev.slot == clock / slot_len, "slot index");
        CHECK(ev.start >= ev.slot * slot_len, "start after slot begin");
        CHECK(ev.start + ev.duration <= (ev.slot + 1U) * slot_len,
              "event fits inside slot");
        CHECK(ev.phase >= -1 && ev.phase <= FABLE_ONE, "phase range");
        if (clock >= ev.start && clock < ev.start + ev.duration) {
            CHECK(ev.phase >= 0 && ev.phase < FABLE_ONE,
                  "phase active during event");
        }
        CHECK(ev.next_start >= (ev.slot + 1U) * slot_len,
              "next start in next slot");
    }
    /* Phase is non-decreasing through one event. */
    fable_event_t first;
    fable_schedule(5U * slot_len, slot_len, duration, 42, &first);
    int32_t prev = -1;
    for (uint32_t c = first.start; c < first.start + first.duration;
         c += 50U) {
        fable_event_t ev;
        fable_schedule(c, slot_len, duration, 42, &ev);
        CHECK(ev.phase >= prev, "phase monotone");
        prev = ev.phase;
    }
    /* Degenerate parameters stay safe. */
    fable_event_t ev;
    fable_schedule(1234, 0, 0, 1, &ev);
    fable_schedule(1234, 10, 100, 1, &ev);
    CHECK(ev.duration <= 10U, "duration clamped to slot");
}

int main(void)
{
    if (!fable_shift_is_arithmetic()) {
        printf("FAIL: platform lacks arithmetic right shift\n");
        return 1;
    }
    test_eases();
    test_trig();
    test_isqrt();
    test_noise();
    test_schedule();
    return fable_test_finish("test_ease");
}
