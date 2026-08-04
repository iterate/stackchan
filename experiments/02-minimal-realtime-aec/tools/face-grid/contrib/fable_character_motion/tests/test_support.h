#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Minimal test harness: CHECK records a failure and keeps going so one
 * run reports every broken invariant; fable_test_finish prints the
 * verdict and returns the process exit code.
 */
static int fable_test_failures = 0;
static int fable_test_checks = 0;

#define CHECK(cond, ...)                                             \
    do {                                                             \
        fable_test_checks++;                                         \
        if (!(cond)) {                                               \
            fable_test_failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);              \
            printf(__VA_ARGS__);                                     \
            printf("\n");                                            \
        }                                                            \
    } while (0)

static inline int fable_test_finish(const char *name)
{
    if (fable_test_failures == 0) {
        printf("PASS %s (%d checks)\n", name, fable_test_checks);
        return 0;
    }
    printf("FAIL %s: %d of %d checks failed\n", name,
           fable_test_failures, fable_test_checks);
    return 1;
}

/*
 * The engine relies on arithmetic right shift for negative operands
 * (implementation-defined in ISO C, arithmetic on every supported
 * toolchain). Golden frames are only meaningful where this holds.
 */
static inline int fable_shift_is_arithmetic(void)
{
    volatile int32_t minus_one = -1;
    volatile int64_t minus_one64 = -1;
    return (minus_one >> 1) == -1 && (minus_one64 >> 1) == -1;
}

/* Reflected CRC-32 (poly 0xEDB88320), bitwise, table-free. */
static inline uint32_t fable_crc32(uint32_t crc, const void *data,
                                   size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}
