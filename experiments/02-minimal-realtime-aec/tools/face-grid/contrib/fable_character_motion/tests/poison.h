#pragma once

/*
 * Injected via -include when building the `poison` target: any use of a
 * floating-point type in the library sources becomes a compile error.
 * The system headers the library is allowed to use are pulled in first
 * (their include guards make later includes no-ops), so only tokens in
 * our own code can trip the poison. Tests and tools are exempt (they
 * report timings).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#pragma GCC poison float double
