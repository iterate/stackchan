#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sprite_face.h"

#include "ega_sorcerer_atlas.h"
#include "handheld_gobbo_atlas.h"
#include "terminal_operator_atlas.h"
#include "vga_navigator_atlas.h"

#include "crc32.h"
#include "scenario.h"

typedef struct {
    const char *name;
    const sprite_atlas_t *atlas;
} atlas_entry_t;

static const atlas_entry_t ATLASES[] = {
    { "ega_sorcerer", &ega_sorcerer_atlas },
    { "handheld_gobbo", &handheld_gobbo_atlas },
    { "vga_navigator", &vga_navigator_atlas },
    { "terminal_operator", &terminal_operator_atlas },
};

enum {
    ATLAS_COUNT = sizeof(ATLASES) / sizeof(ATLASES[0]),
};

static int failures;

#define CHECK(condition, ...)                                          \
    do {                                                               \
        if (!(condition)) {                                            \
            ++failures;                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                \
            printf(__VA_ARGS__);                                       \
            printf("\n");                                              \
        }                                                              \
    } while (0)

static uint16_t frame_buffer[SPRITE_FACE_PIXEL_COUNT];
static uint16_t frame_buffer_b[SPRITE_FACE_PIXEL_COUNT];

static uint32_t run_scenario_crc(
    const sprite_atlas_t *atlas, uint16_t *pixels)
{
    sprite_face_t face;
    if (!sprite_face_init(&face, atlas)) {
        return 0;
    }
    uint32_t crc = 0;
    face_keyframe_t keyframe;
    for (uint32_t index = 0; index < SCENARIO_FRAME_COUNT; ++index) {
        scenario_keyframe(index, &keyframe);
        if (!sprite_face_render(
                &face, &keyframe, scenario_clock(index), pixels,
                SPRITE_FACE_PIXEL_COUNT)) {
            return 0;
        }
        crc = crc32_update(
            crc, pixels, sizeof(uint16_t) * SPRITE_FACE_PIXEL_COUNT);
    }
    return crc;
}

static void test_init_and_validation(void)
{
    for (size_t index = 0; index < ATLAS_COUNT; ++index) {
        sprite_face_t face;
        CHECK(
            sprite_face_init(&face, ATLASES[index].atlas),
            "init rejects valid atlas %s", ATLASES[index].name);
    }

    sprite_face_t face;
    CHECK(!sprite_face_init(&face, NULL), "init accepts NULL atlas");
    CHECK(!sprite_face_init(NULL, ATLASES[0].atlas),
          "init accepts NULL face");

    sprite_atlas_t broken = *ATLASES[0].atlas;
    broken.magic = 0x12345678u;
    CHECK(!sprite_face_init(&face, &broken), "bad magic accepted");

    broken = *ATLASES[0].atlas;
    broken.version = 999;
    CHECK(!sprite_face_init(&face, &broken), "bad version accepted");

    broken = *ATLASES[0].atlas;
    broken.scale = 3;
    CHECK(!sprite_face_init(&face, &broken), "bad scale accepted");

    broken = *ATLASES[0].atlas;
    broken.native_width = 200;
    CHECK(!sprite_face_init(&face, &broken),
          "oversized native width accepted");

    broken = *ATLASES[0].atlas;
    broken.blob_size = 4;
    CHECK(!sprite_face_init(&face, &broken),
          "truncated blob accepted");

    broken = *ATLASES[0].atlas;
    broken.palette_count = 2;
    CHECK(!sprite_face_init(&face, &broken),
          "palette smaller than used indices accepted");
}

static void test_render_guards(void)
{
    sprite_face_t face;
    face_keyframe_t keyframe;
    scenario_keyframe(0, &keyframe);
    CHECK(sprite_face_init(&face, ATLASES[0].atlas), "init failed");
    CHECK(
        !sprite_face_render(
            &face, &keyframe, 0, frame_buffer,
            SPRITE_FACE_PIXEL_COUNT - 1),
        "undersized buffer accepted");
    CHECK(
        !sprite_face_render(&face, NULL, 0, frame_buffer,
                            SPRITE_FACE_PIXEL_COUNT),
        "NULL keyframe accepted");
    CHECK(
        sprite_face_render(&face, &keyframe, 0, frame_buffer,
                           SPRITE_FACE_PIXEL_COUNT),
        "exact-capacity render failed");
}

static void test_determinism(void)
{
    for (size_t index = 0; index < ATLAS_COUNT; ++index) {
        const uint32_t first =
            run_scenario_crc(ATLASES[index].atlas, frame_buffer);
        const uint32_t second =
            run_scenario_crc(ATLASES[index].atlas, frame_buffer_b);
        CHECK(first != 0, "%s scenario failed", ATLASES[index].name);
        CHECK(
            first == second,
            "%s not deterministic across fresh engines "
            "(%08x vs %08x)",
            ATLASES[index].name, first, second);
    }

    /* A clock jump backwards resets state and replays identically. */
    sprite_face_t face;
    face_keyframe_t keyframe;
    CHECK(sprite_face_init(&face, ATLASES[0].atlas), "init failed");
    scenario_keyframe(100, &keyframe);
    sprite_face_render(
        &face, &keyframe, scenario_clock(100), frame_buffer,
        SPRITE_FACE_PIXEL_COUNT);
    uint32_t replay = 0;
    for (uint32_t index = 0; index < SCENARIO_FRAME_COUNT; ++index) {
        scenario_keyframe(index, &keyframe);
        sprite_face_render(
            &face, &keyframe, scenario_clock(index), frame_buffer,
            SPRITE_FACE_PIXEL_COUNT);
        replay = crc32_update(
            replay, frame_buffer,
            sizeof(uint16_t) * SPRITE_FACE_PIXEL_COUNT);
    }
    const uint32_t fresh =
        run_scenario_crc(ATLASES[0].atlas, frame_buffer_b);
    CHECK(
        replay == fresh,
        "clock regression does not reset to a clean replay");
}

static void test_shape_coverage(void)
{
    const sprite_selector_t selector = SPRITE_SELECTOR_DEFAULTS;
    uint32_t seen = 0;
    face_keyframe_t keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    for (uint32_t open = 0; open < 256u; open += 15u) {
        for (uint32_t width = 0; width < 256u; width += 45u) {
            for (uint32_t round = 0; round < 256u; round += 45u) {
                for (uint32_t teeth = 0; teeth < 256u; teeth += 45u) {
                    for (uint32_t press = 0; press < 256u;
                         press += 85u) {
                        keyframe.mouth_open = (uint8_t)open;
                        keyframe.mouth_width = (uint8_t)width;
                        keyframe.mouth_round = (uint8_t)round;
                        keyframe.mouth_teeth = (uint8_t)teeth;
                        keyframe.mouth_press = (uint8_t)press;
                        const uint8_t shape =
                            sprite_face_select_shape(
                                &selector, &keyframe);
                        CHECK(
                            shape < SPRITE_MOUTH_SHAPE_COUNT,
                            "selector returned %u", shape);
                        seen |= 1u << shape;
                    }
                }
            }
        }
    }
    CHECK(
        seen == (1u << SPRITE_MOUTH_SHAPE_COUNT) - 1u,
        "selector cannot reach every canonical shape (mask %03x)",
        seen);
}

static void test_debounce(void)
{
    /* Rapidly alternating articulation must never flip the shown
     * shape faster than the minimum hold, and a pause shorter than
     * the close delay must not snap to rest. */
    sprite_face_t face;
    CHECK(sprite_face_init(&face, ATLASES[0].atlas), "init failed");
    const sprite_timing_t *timing = &ATLASES[0].atlas->timing;
    face_keyframe_t keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.eye_left_open = 255;
    keyframe.eye_right_open = 255;
    keyframe.flags = FACE_KEYFRAME_FLAG_SPEAKING;

    uint8_t previous_shape = SPRITE_MOUTH_X;
    uint32_t last_change = 0;
    uint32_t changes = 0;
    for (uint32_t index = 0; index < 240u; ++index) {
        const uint32_t clock = scenario_clock(index);
        keyframe.mouth_open = (index & 1u) ? 250u : 0u;
        keyframe.mouth_width = 128;
        sprite_face_render(
            &face, &keyframe, clock, frame_buffer,
            SPRITE_FACE_PIXEL_COUNT);
        if (face.current_shape != previous_shape) {
            if (changes > 0) {
                CHECK(
                    clock - last_change >= timing->mouth_min_hold,
                    "shape flipped after %u samples (min hold %u)",
                    clock - last_change, timing->mouth_min_hold);
            }
            /* Closing all the way to rest additionally needs the
             * target to persist for the close delay. */
            if (face.current_shape == SPRITE_MOUTH_X && changes > 0) {
                CHECK(
                    clock - face.target_since >=
                        timing->mouth_close_delay,
                    "rest reached before the close delay");
            }
            previous_shape = face.current_shape;
            last_change = clock;
            ++changes;
        }
    }
    CHECK(changes > 0, "debounce test never changed shape");
}

static int golden_path(char *buffer, size_t size, const char *base)
{
    return snprintf(buffer, size, "%s/golden_crcs.txt", base) <
        (int)size;
}

static void test_golden(const char *golden_dir, int update)
{
    char path[512];
    if (!golden_path(path, sizeof(path), golden_dir)) {
        CHECK(0, "golden path too long");
        return;
    }
    uint32_t crcs[ATLAS_COUNT];
    for (size_t index = 0; index < ATLAS_COUNT; ++index) {
        crcs[index] =
            run_scenario_crc(ATLASES[index].atlas, frame_buffer);
        CHECK(crcs[index] != 0, "%s scenario failed",
              ATLASES[index].name);
    }
    if (update) {
        FILE *handle = fopen(path, "w");
        if (handle == NULL) {
            CHECK(0, "cannot write %s", path);
            return;
        }
        for (size_t index = 0; index < ATLAS_COUNT; ++index) {
            fprintf(handle, "%s %08x\n",
                    ATLASES[index].name, crcs[index]);
        }
        fclose(handle);
        printf("golden CRCs updated in %s\n", path);
        return;
    }
    FILE *handle = fopen(path, "r");
    if (handle == NULL) {
        CHECK(0, "missing golden file %s (run with --update)", path);
        return;
    }
    char name[128];
    unsigned int value;
    size_t matched = 0;
    while (fscanf(handle, "%127s %8x", name, &value) == 2) {
        for (size_t index = 0; index < ATLAS_COUNT; ++index) {
            if (strcmp(name, ATLASES[index].name) == 0) {
                CHECK(
                    crcs[index] == value,
                    "%s golden CRC drift: got %08x want %08x",
                    name, crcs[index], value);
                ++matched;
            }
        }
    }
    fclose(handle);
    CHECK(
        matched == ATLAS_COUNT,
        "golden file lists %zu of %d atlases", matched,
        (int)ATLAS_COUNT);
}

static void dump_frames(const char *directory)
{
    for (size_t index = 0; index < ATLAS_COUNT; ++index) {
        sprite_face_t face;
        if (!sprite_face_init(&face, ATLASES[index].atlas)) {
            continue;
        }
        face_keyframe_t keyframe;
        for (uint32_t frame = 0; frame < SCENARIO_FRAME_COUNT;
             frame += 5u) {
            scenario_keyframe(frame, &keyframe);
            sprite_face_render(
                &face, &keyframe, scenario_clock(frame),
                frame_buffer, SPRITE_FACE_PIXEL_COUNT);
            char path[512];
            snprintf(
                path, sizeof(path), "%s/%s_%03u.ppm", directory,
                ATLASES[index].name, frame);
            FILE *handle = fopen(path, "wb");
            if (handle == NULL) {
                printf("cannot write %s\n", path);
                return;
            }
            fprintf(handle, "P6\n%d %d\n255\n",
                    SPRITE_FACE_WIDTH, SPRITE_FACE_HEIGHT);
            for (int32_t pixel = 0;
                 pixel < SPRITE_FACE_PIXEL_COUNT; ++pixel) {
                const uint16_t value = frame_buffer[pixel];
                const uint8_t rgb[3] = {
                    (uint8_t)(((value >> 11u) & 31u) * 255u / 31u),
                    (uint8_t)(((value >> 5u) & 63u) * 255u / 63u),
                    (uint8_t)((value & 31u) * 255u / 31u),
                };
                fwrite(rgb, 1, 3, handle);
            }
            fclose(handle);
        }
    }
    printf("frames dumped to %s\n", directory);
}

/* Raw RGB565 dumps of a few mid-scenario frames, used by the WASM
 * harness for byte-for-byte comparison against the browser build. */
static const uint32_t RAW_FRAMES[] = { 0, 100, 250 };

static void dump_raw(const char *directory)
{
    for (size_t index = 0; index < ATLAS_COUNT; ++index) {
        sprite_face_t face;
        if (!sprite_face_init(&face, ATLASES[index].atlas)) {
            continue;
        }
        face_keyframe_t keyframe;
        for (uint32_t frame = 0; frame < SCENARIO_FRAME_COUNT;
             ++frame) {
            scenario_keyframe(frame, &keyframe);
            sprite_face_render(
                &face, &keyframe, scenario_clock(frame),
                frame_buffer, SPRITE_FACE_PIXEL_COUNT);
            for (size_t mark = 0;
                 mark < sizeof(RAW_FRAMES) / sizeof(RAW_FRAMES[0]);
                 ++mark) {
                if (RAW_FRAMES[mark] != frame) {
                    continue;
                }
                char path[512];
                snprintf(
                    path, sizeof(path), "%s/%s_f%03u.bin",
                    directory, ATLASES[index].name, frame);
                FILE *handle = fopen(path, "wb");
                if (handle == NULL) {
                    printf("cannot write %s\n", path);
                    return;
                }
                fwrite(frame_buffer, sizeof(uint16_t),
                       SPRITE_FACE_PIXEL_COUNT, handle);
                fclose(handle);
            }
        }
    }
    printf("raw frames dumped to %s\n", directory);
}

static void benchmark(void)
{
    printf("\nsizes: sprite_face_t=%zu B, sprite_atlas_t=%zu B, "
           "sprite_bank_t=%zu B, sprite_cell_t=%zu B, "
           "frame=%zu B\n",
           sizeof(sprite_face_t), sizeof(sprite_atlas_t),
           sizeof(sprite_bank_t), sizeof(sprite_cell_t),
           sizeof(frame_buffer));
    for (size_t index = 0; index < ATLAS_COUNT; ++index) {
        const sprite_atlas_t *atlas = ATLASES[index].atlas;
        sprite_face_t face;
        sprite_face_init(&face, atlas);
        face_keyframe_t keyframe;
        const uint32_t iterations = 2000;
        struct timespec begin;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &begin);
        for (uint32_t i = 0; i < iterations; ++i) {
            scenario_keyframe(i % SCENARIO_FRAME_COUNT, &keyframe);
            sprite_face_render(
                &face, &keyframe,
                scenario_clock(i % SCENARIO_FRAME_COUNT),
                frame_buffer, SPRITE_FACE_PIXEL_COUNT);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        const double elapsed =
            (double)(end.tv_sec - begin.tv_sec) * 1e9 +
            (double)(end.tv_nsec - begin.tv_nsec);
        const double per_frame_us = elapsed / iterations / 1e3;
        uint32_t flash = atlas->blob_size +
            (uint32_t)atlas->palette_count * 2u +
            (uint32_t)atlas->cell_count *
                (uint32_t)sizeof(sprite_cell_t) +
            (uint32_t)atlas->bank_count *
                (uint32_t)sizeof(sprite_bank_t);
        printf(
            "%-18s %7.1f us/frame (%6.0f fps native), "
            "%2u cells, blob %5u B, flash ~%5u B\n",
            ATLASES[index].name, per_frame_us,
            1e6 / per_frame_us, atlas->cell_count,
            atlas->blob_size, flash);
    }
}

int main(int argc, char **argv)
{
    const char *golden_dir = "tests";
    const char *dump_dir = NULL;
    const char *raw_dir = NULL;
    int update = 0;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--update") == 0) {
            update = 1;
        } else if (strcmp(argv[index], "--golden-dir") == 0 &&
                   index + 1 < argc) {
            golden_dir = argv[++index];
        } else if (strcmp(argv[index], "--dump") == 0 &&
                   index + 1 < argc) {
            dump_dir = argv[++index];
        } else if (strcmp(argv[index], "--raw") == 0 &&
                   index + 1 < argc) {
            raw_dir = argv[++index];
        }
    }

    test_init_and_validation();
    test_render_guards();
    test_determinism();
    test_shape_coverage();
    test_debounce();
    test_golden(golden_dir, update);
    if (dump_dir != NULL) {
        dump_frames(dump_dir);
    }
    if (raw_dir != NULL) {
        dump_raw(raw_dir);
    }
    benchmark();

    if (failures == 0) {
        printf("\nOK: all sprite-face tests passed\n");
        return 0;
    }
    printf("\n%d FAILURE(S)\n", failures);
    return 1;
}
