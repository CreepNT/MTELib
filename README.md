# MTELib
A small, single-header library to provide easier usage of the ARM *Memory Tagging Extensions*.

# Configuration
Features can be enabled/disabled using `#define`s.

| `#define` | Effect | Notes |
| :-------: | :----- | :---- |
| `MTELIB_NO_TAG_CHECKS` | Disables checking of tags provided to functions. |
| `MTELIB_NO_INTRINSICS` | Disables usage of intrinsics from `<arm_acle.h>` | Not compatible with `MTELIB_NO_INLINE_ASSEMBLY`
| `MTELIB_NO_INLINE_ASSEMBLY` | Disables usage of inline assembly | Not compatible with `MTELIB_NO_INTRINSICS`
| `MTELIB_DISABLE_DGRANULE_OPERATIONS` | Disable usage of double-granule instructions | Only effective if `MTELIB_NO_INLINE_ASSEMBLY` isn't set
| `MTELIB_NO_ALIGNMENT_CHECKS` | Disables **ALL** alignment checks | Make sure all pointers and sizes you provide are aligned *when required* or hardware aborts (e.g. `SIGSEGV`) will occur
| `MTELIB_RELAXED_ALIGNMENT_CHECKS` | Disables *some* alignment checks, when they are not required | Read function descriptions carefully, as misaligned pointers/sizes can cause unexpected behaviour