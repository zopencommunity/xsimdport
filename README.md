# xsimd for z/OS

[xsimd](https://github.com/xtensor-stack/xsimd) 14.3.0 — header-only C++
wrappers over SIMD intrinsics.

> [!WARNING]
> **The headers install, but `xsimd::batch` cannot be instantiated here.**
> Anything that uses xsimd batches — Arrow C++ included — will not build yet.
> This port makes the headers available and records the diagnosis; it does not
> make xsimd usable.

## Why this port exists

Arrow C++ requires xsimd, and not optionally. From Arrow's own
`ThirdpartyToolchain.cmake`:

```cmake
# Xsimd is mandatory as its CPU feature detection is the basis for Arrow CpuInfo
resolve_dependency(xsimd ...)
```

So xsimd is on the critical path for Arrow, which makes its state worth pinning
down precisely rather than discovering inside a six-hour Arrow build.

## What is wrong

There are two states and neither works.

**Without vector flags** xsimd detects no SIMD architecture at all, and any use
of a batch type is a hard compile error:

```
static_assert failed due to requirement 'supported_architecture'
"No SIMD architecture detected, cannot instantiate a batch"
```

The reason is a macro spelling. xsimd's gate for the z Vector Extension is:

```c
#if defined(__VEC__) && __VEC__ >= 10304 && __ARCH__ >= 12
#define XSIMD_WITH_VXE 1
```

and z/OS defaults to `__ARCH__ 10` with no `__VEC__` at all — it defines
`__VX__` instead, which is the z/OS spelling.

**With `-march=z14 -fzvector`** both macros appear (`__ARCH__ 12`,
`__VEC__ 10304`), xsimd correctly enables VXE, and then fails differently:

```
/usr/lpp/IBM/oelcpp/v2r0/lib/clang/14.0.0/include/vecintrin.h:30:1:
error: expected unqualified-id
```

That is IBM's own vector-intrinsics header, and the cause is not xsimd:

| included first | `#include <vecintrin.h>` |
| --- | --- |
| *nothing* | OK |
| `<type_traits>`, `<cstddef>`, `<limits>`, `<cstdint>` | OK |
| **`<complex>`** | **breaks** |

`vecintrin.h` is not self-contained in C++ once `<complex>` has been included,
and xsimd includes `<complex>` because it supports complex batches. Forcing
`-include vecintrin.h` ahead of everything does not help either.

So the blocker is a compiler-header defect, not a porting gap in xsimd. Worth
raising with the compiler team — a header that only parses when included first
is a bug regardless of who hits it.

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

## What would unblock it

Any one of:

1. A fix to `vecintrin.h` so it is self-contained in C++ (the real fix).
2. An xsimd change that includes `<vecintrin.h>` before `<complex>` reaches
   the translation unit — harder than it sounds, since the include order is
   driven by the consumer.
3. Getting xsimd's emulated (scalar) backend working. `XSIMD_WITH_EMULATED`
   exists and gets past the "no SIMD architecture" assert, but needs the
   default arch pointed at `xsimd::emulated<N>` as well; that was not pursued.
   It would cost the vectorisation but unblock Arrow, which is likely the right
   trade for a first port.

Option 3 is the one to try first if Arrow is the goal.

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
