/*
 * xsimd defaults for z/OS.
 *
 * Force-included into anything that builds against this port, because the
 * settings it carries cannot survive as ordinary -D flags. The value of
 * XSIMD_DEFAULT_ARCH contains angle brackets:
 *
 *     -DXSIMD_DEFAULT_ARCH=xsimd::emulated<128>
 *
 * and by the time a build system has passed that through a shell, "<128>" has
 * been read as a redirection:
 *
 *     bad file descriptor "128"
 *
 * which is what Arrow's CMake compiler probe failed with. Putting the
 * definitions in a header and force-including it keeps shell metacharacters
 * out of the command line entirely. zoslib does the same thing with its
 * zos-v2r5-symbolfixes.h.
 *
 * Why emulated: xsimd's z Vector Extension path includes <vecintrin.h>, and
 * that header does not parse in C++ once <complex> has been included, which
 * xsimd does. The emulated backend implements batches with scalar code -- no
 * intrinsics, correct results, no vectorisation. See the port's README.
 *
 * Both are guarded, so a consumer that wants to make its own choice can define
 * either before this is seen and keep it.
 */

#ifndef XSIMD_ZOS_DEFAULTS_H
#define XSIMD_ZOS_DEFAULTS_H

#ifndef XSIMD_WITH_EMULATED
#define XSIMD_WITH_EMULATED 1
#endif

#ifndef XSIMD_DEFAULT_ARCH
#define XSIMD_DEFAULT_ARCH xsimd::emulated<128>
#endif

#endif /* XSIMD_ZOS_DEFAULTS_H */
