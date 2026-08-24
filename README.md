# xsimd for z/OS

[xsimd](https://github.com/xtensor-stack/xsimd) 14.3.0 — header-only C++
wrappers over SIMD intrinsics.

**Working, through the emulated backend.** Batches are implemented with scalar
code: correct results, no vectorisation. If you use the port through zopen the
flags are applied for you.

## Why this port exists

Arrow C++ requires xsimd, and not optionally. From Arrow's own
`ThirdpartyToolchain.cmake`:

```cmake
# Xsimd is mandatory as its CPU feature detection is the basis for Arrow CpuInfo
resolve_dependency(xsimd ...)
```

So xsimd is on the critical path for Arrow, which makes its state worth pinning
down precisely rather than discovering inside a six-hour Arrow build.

## How it works, and why it is scalar

The machine is not the limitation. It has vector hardware, and a plain vector
add compiles and runs correctly at `-march=z14`:

```
vec: 11 22 33 44
```

The limitation is that xsimd's vector path cannot be compiled here, in two
stages.

**First, nothing is detected.** xsimd gates the z Vector Extension on:

```c
#if defined(__VEC__) && __VEC__ >= 10304 && __ARCH__ >= 12
#define XSIMD_WITH_VXE 1
```

z/OS defaults to `__ARCH__ 10` and spells the macro `__VX__`, not `__VEC__`, so
`XSIMD_WITH_VXE` is 0 and any use of a batch type is a hard error:

```
static_assert failed: "No SIMD architecture detected, cannot instantiate a batch"
```

**Second, fixing that exposes a compiler-header defect.** `-march=z14 -fzvector`
supplies both macros (`__ARCH__ 12`, `__VEC__ 10304`), xsimd correctly enables
VXE — and then includes `<vecintrin.h>`, which fails to parse:

```
/usr/lpp/IBM/oelcpp/v2r0/lib/clang/14.0.0/include/vecintrin.h:30:1:
error: expected unqualified-id
```

That is IBM's own header, and the trigger is include order:

| included first | `#include <vecintrin.h>` |
| --- | --- |
| *nothing*, `<type_traits>`, `<cstddef>`, `<limits>`, `<cstdint>` | OK |
| **`<complex>`** | **breaks** |

`vecintrin.h` is only self-contained when included first, and xsimd includes
`<complex>` because it supports complex batches. **This is worth raising with
the compiler team** — it will hit anyone doing s390x vector work in C++, not
just xsimd.

## What the port does instead

It selects xsimd's emulated backend, which implements batches with scalar code
and never includes `<vecintrin.h>`:

```
arch=emulated lanes=4
2*3 = 6.0
```

The settings are delivered as a **force-included header**, not as `-D` flags,
and that is not a stylistic choice. `XSIMD_DEFAULT_ARCH`'s value contains angle
brackets, and once a build system has passed

```
-DXSIMD_DEFAULT_ARCH=xsimd::emulated<128>
```

through a shell, `<128>` has been read as a redirection:

```
bad file descriptor "128"
```

which is exactly how Arrow's CMake compiler probe failed. `xsimd-zos-defaults.h`
keeps shell metacharacters off the command line; zoslib delivers its
`zos-v2r5-symbolfixes.h` the same way. Both macros in it are guarded, so a
consumer that wants to choose differently can define either first and keep it.

128 bits is the natural width to ask for — it matches the z vector registers, so
if the vector path is ever fixed the lane counts do not change underneath
anyone.

Dependents receive the include path and the force-include automatically through
`zopen_append_to_env`, because a consumer that gets the headers without them
compiles against an xsimd that cannot produce a batch and finds out deep inside
its own build. For Arrow that would be several hundred targets in.

The check asserts both halves on every build: that batches instantiate, and
that they compute the right answer. The second matters more — scalar code
standing in for vector code would fail silently.

## The other z/OS patch: posix_memalign

`xsimd_aligned_allocator.hpp` allocates with `posix_memalign`, which is
unusable here below the z/OS 3.1 library level. zoslib's `stdlib.h` does:

```c
// LE fix since posix_memalign is exposed in 2.5
#if (__TARGET_LIB__ < 0x43010000)
#define posix_memalign __posix_memalign_replaced
#endif
```

and nothing anywhere declares or defines `__posix_memalign_replaced` — it is
not in `libzoslib.a` either. The name is effectively poisoned, so any use fails
to compile. zopen targets `zosv2r5` by default, so this is the ordinary case
rather than an edge one, and it needs **both** `ZOSLIB_OVERRIDE_CLIB=1` and the
older target to bite — which is why it does not show up in a casual test.

The patch uses `aligned_alloc`, whose result is released with plain `free()` —
which is what `xaligned_free` already does. One measured detail matters: z/OS
enforces the C11 rule that the size be an integer multiple of the alignment.

| call | result |
| --- | --- |
| `aligned_alloc(64, 100)` | **NULL** |
| `aligned_alloc(64, 128)` | ok, correctly aligned |
| `aligned_alloc(4096, 4096)` | ok, correctly aligned |

Most platforms relax that; this one does not, so the request is rounded up.

## Things already ruled out

* **Not the macro spelling alone.** Patching xsimd's gate to accept `__VX__`
  gets past detection and then lands on the same `vecintrin.h` failure. Worse,
  defining `__VEC__` on the command line to satisfy xsimd actively breaks
  `vecintrin.h`, which reads that macro itself to decide what to declare.
* **Not C++ mode.** `vecintrin.h` compiles fine on its own in C++ at `-std=c++14`,
  `c++17` and the default, with `-fzvector -march=z14`.
* **Not the arch level.** `z14`, `z15` and `z16` all behave identically.
* **The hardware is fine.** A plain vector add compiled with `-march=z14` runs
  correctly: `vec: 11 22 33 44`.

## Getting the vector path back

The emulated backend costs vectorisation, so it is worth revisiting if
`<vecintrin.h>` is ever fixed. The check reports the vector path's status on
every build, so the day it starts compiling will not be missed — at which point
the emulated default can simply be dropped.

The other route, if the header is not going to change, is an xsimd patch that
keeps `<vecintrin.h>` out of any translation unit that has seen `<complex>`.
That is harder than it sounds, because the include order is driven by the
consumer rather than by xsimd.

## Installing

```sh
zopen install xsimd
```

Header-only, so the port installs headers and a CMake config and nothing else.
For a consumer that wants to try the vector path:

```sh
-march=z14 -fzvector
```

which is what `XSIMD_ZOS_CXXFLAGS` in the buildenv carries.
