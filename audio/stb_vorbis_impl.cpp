// The one translation unit that compiles the vendored Ogg Vorbis decoder (audio/third_party/stb_vorbis.c, stb_vorbis
// v1.22 by Sean Barrett, public domain / MIT). It is vendored rather than linked: the client must build on a bare Linux
// box and cross-build for Windows. The .c file sits outside the module GLOB, so this TU is its only build.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "audio/third_party/stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
