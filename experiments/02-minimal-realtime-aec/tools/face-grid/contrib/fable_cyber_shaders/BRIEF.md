# Fixed-point cyberpunk renderer worker

Research and prototype original MCU software-shader faces using low-resolution
2D SDF approximations, distance-baked glow, RGB565 palette LUTs, Bayer
dithering, scanlines, chromatic offsets, liquid/smooth-min-like blends,
wireframes, voice orbs, single optics, LED matrices, and glitch effects.

No GLSL runtime and no floating point in the final hot paths. Create standalone
portable C, tests/benchmarks, a renderer manifest, and a source/licensing
research report in this directory only. Input/output and performance
constraints are in `../README.md`.
