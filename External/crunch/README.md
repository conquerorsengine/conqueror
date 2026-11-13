# crunch / crnlib (Unity-Technologies/crunch, branch `unity`)

Real Crunch texture compression for "Use Crunch Compression" (Texture Import Settings): at build time `crn_compress`
encodes DXT1 / DXT5 blocks into `.crn`; at load time `crn_decomp.h` transcodes them straight to DXT and the GPU gets the
blocks without driver compression. Zlib license (see `license.txt`), Copyright (c) 2010-2016 Richard Geldreich, Jr. and
Binomial LLC; Unity Technologies improvements.

Only the sources of the original `crnlib/Makefile` are kept (plus `crn_threading_win32.cpp` for Windows builds).

Altered source files (marked `MODIFIED by Conqueror's Engine` in the code):
- `inc/crn_decomp.h`: `#include <stdint.h>` (uintptr_t with current compilers)
- `crnlib/crn_vector.cpp`: explicit `uint64` casts for `is_power_of_2` / `next_pow2` (ambiguous overload on 64-bit Linux)
