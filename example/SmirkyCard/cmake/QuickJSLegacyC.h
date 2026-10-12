/* Force-included into the QuickJS-ng build only when the C compiler has no
   _Alignas (cmake/QuickJS.cmake): MSVC before 19.28, which is what Visual
   Studio 2017's v141 toolset for the Windows XP profile is, and GCC 4.0 for
   the Tiger UB1 profile. QuickJS-ng v0.16.2 uses one C11 keyword, _Alignas,
   on three struct members in quickjs.c; both compilers place the attribute
   at the start of a member declaration. */
#ifndef LOKA_SMIRKYCARD_QUICKJS_LEGACY_C_H
#define LOKA_SMIRKYCARD_QUICKJS_LEGACY_C_H

#if defined(_MSC_VER)
#define _Alignas(n) __declspec(align(n))
#elif defined(__GNUC__)
#define _Alignas(n) __attribute__((aligned(n)))
#else
#error "QuickJSLegacyC.h knows MSVC and GCC only"
#endif

#endif
