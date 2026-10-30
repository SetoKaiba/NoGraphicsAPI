#pragma once

#include <limits.h>
#include <stddef.h>

#if defined(__MINGW32__)
#error "NoGraphicsAPI does not support MinGW"
#endif
#if defined(__EMSCRIPTEN__) && (defined(__wasm32__) || defined(__wasm64__))
// Emscripten wasm32 and wasm64 are supported
#elif defined(_M_ARM64EC) || (!defined(_M_X64) && !defined(__x86_64__) && !(defined(__APPLE__) && defined(__aarch64__)))
#error "NoGraphicsAPI requires x86-64, Apple ARM64, or Emscripten Wasm32/Wasm64"
#endif

typedef signed char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
#if defined(_MSC_VER)
typedef long long int64;
typedef unsigned long long uint64;
#else
typedef __INT64_TYPE__ int64;
typedef __UINT64_TYPE__ uint64;
#endif
typedef unsigned char byte;
#if defined(__wasm32__)
typedef __UINTPTR_TYPE__ uintptr;
#else
typedef uint64 uintptr;
#endif

static_assert(CHAR_BIT == 8 && sizeof(int8) == 1 && sizeof(uint8) == 1);
static_assert(sizeof(int16) == 2 && sizeof(uint16) == 2 && sizeof(int32) == 4 && sizeof(uint32) == 4);
#if defined(__wasm32__)
static_assert(sizeof(int64) == 8 && sizeof(uint64) == 8 && sizeof(void*) == 4 && sizeof(size_t) == 4 && sizeof(uintptr) == 4);
#else
static_assert(sizeof(int64) == 8 && sizeof(uint64) == 8 && sizeof(void*) == 8 && sizeof(size_t) == 8 && sizeof(uintptr) == 8);
#endif

namespace gpu
{
using ::int8;
using ::uint8;
using ::int16;
using ::uint16;
using ::int32;
using ::uint32;
using ::int64;
using ::uint64;
using ::byte;
using ::uintptr;
using ::size_t;
inline constexpr size_t maximum_alignment = alignof(long double);
} // namespace gpu
