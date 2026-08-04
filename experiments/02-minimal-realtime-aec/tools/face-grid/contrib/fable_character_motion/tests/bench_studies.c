#include "../src/fable_studies.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Host benchmark: wall-clock per frame for the motion engine alone and
 * for every study. Host numbers only bound the shape of the cost (all
 * integer ops, span fills); the README extrapolates to ESP32-S3.
 */

static uint16_t frame[FABLE_STUDY_PIXELS];

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void)
{
    fable_keyframe_t kf;
    memset(&kf, 0, sizeof(kf));
    kf.mouth_open = 120;
    kf.mouth_width = 150;
    kf.eye_left_open = 255;
    kf.eye_right_open = 255;
    kf.expression = FABLE_ACTIVITY_SPEAKING;
    kf.flags = FABLE_KEYFRAME_FLAG_SPEAKING;

    enum { EVALS = 200000, FRAMES = 600 };

    volatile int32_t sink = 0;
    uint64_t t0 = now_ns();
    for (uint32_t i = 0; i < (uint32_t)EVALS; i++) {
        fable_motion_pose_t pose;
        fable_motion_eval(&FABLE_PERSONA_CURIOUS, &kf, i * 533U, &pose);
        sink += pose.eye_x_q2;
    }
    uint64_t t1 = now_ns();
    printf("motion eval: %8.1f ns/eval (%d evals)\n",
           (double)(t1 - t0) / (double)EVALS, EVALS);

    for (uint32_t s = 0; s < (uint32_t)FABLE_STUDY_COUNT; s++) {
        t0 = now_ns();
        for (uint32_t f = 0; f < (uint32_t)FRAMES; f++) {
            fable_study_render((fable_study_t)s, &kf, f * 533U, frame,
                               FABLE_STUDY_PIXELS);
        }
        t1 = now_ns();
        const double per_frame_us =
            (double)(t1 - t0) / (double)FRAMES / 1000.0;
        printf("%-14s %8.1f us/frame  (%6.0f fps host)\n",
               fable_study_slug((fable_study_t)s), per_frame_us,
               1000000.0 / per_frame_us);
    }
    (void)sink;
    return 0;
}
