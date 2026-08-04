#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "speech_leveler.h"

static void test_gain_range(void)
{
    assert(!speech_leveler_gain_is_valid(-1));
    assert(speech_leveler_gain_is_valid(0));
    assert(speech_leveler_gain_is_valid(8));
    assert(speech_leveler_gain_is_valid(12));
    assert(!speech_leveler_gain_is_valid(13));
}

static void test_silence_and_linear_gain(void)
{
    int16_t samples[] = {0, 1000, -1000};
    speech_leveler_metrics_t metrics;
    speech_leveler_process(samples, 3, 8, &metrics);

    assert(samples[0] == 0);
    assert(samples[1] == 2512);
    assert(samples[2] == -2512);
    assert(metrics.processed_samples == 3);
    assert(metrics.limited_samples == 0);
    assert(metrics.overrange_samples == 0);
    assert(metrics.source_peak == 1000);
    assert(metrics.pre_limiter_peak == 2512);
    assert(metrics.output_peak == 2512);
}

static void test_limiter_is_symmetric_and_never_clips(void)
{
    int16_t samples[] = {32767, -32768, 30000, -30000};
    speech_leveler_metrics_t metrics;
    speech_leveler_process(samples, 4, 8, &metrics);

    for (size_t index = 0; index < 4; ++index) {
        assert(samples[index] <= 32767);
        assert(samples[index] >= -32767);
    }
    assert(samples[0] == -samples[1] ||
           samples[0] == -samples[1] - 1);
    assert(samples[2] == -samples[3]);
    assert(metrics.processed_samples == 4);
    assert(metrics.limited_samples == 4);
    assert(metrics.overrange_samples == 4);
    assert(metrics.source_peak == 32768);
    assert(metrics.pre_limiter_peak > 32767);
    assert(metrics.output_peak < 32767);
}

static void test_limiter_is_monotonic(void)
{
    int16_t samples[] = {1000, 5000, 10000, 15000, 20000, 25000, 30000};
    speech_leveler_process(samples, 7, 8, NULL);
    for (size_t index = 1; index < 7; ++index) {
        assert(samples[index] > samples[index - 1]);
    }
}

int main(void)
{
    test_gain_range();
    test_silence_and_linear_gain();
    test_limiter_is_symmetric_and_never_clips();
    test_limiter_is_monotonic();
    puts("speech_leveler_test: PASS");
    return 0;
}
