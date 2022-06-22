/**
 * @file mtelib.h
 * @author CreepNT
 * @brief Wrapper around ASM primitives for MTE
 * 
 * @copyright Copyright (c) CreepNT 2022
 * @todo Verify emitted assembly is correct
 * @todo If NO_INTRINSICS, add replacement for some intrinsics
 * @todo Add support for DC GVA
 */

#include <assert.h> //assert
#include <stddef.h> //size_t
#include <stdint.h> //uint64_t, uintptr_t

#ifndef MTE_ASSERT
    #define MTE_ASSERT(condition, errormsg) assert(condition && __FILE__ && __LINE__ && errormsg)
#endif

#ifndef MTE_ASM
    #define MTE_ASM __asm__ volatile
#endif

/* Configuration options */
//MTELIB_NO_TAG_CHECKS: disables tags checks.
//MTELIB_NO_ALIGNMENT_CHECKS: disables ALL alignment checks. Only use this if you can guarantee alignment.
//MTELIB_RELAXED_ALIGNMENT_CHECKS: disables alignment checks for functions in which misaligned data is OK.
//MTELIB_NO_INTRINSICS: disables usage of intrinsics.
//MTELIB_NO_INLINE_ASSEMBLY: disables usage of inline assembly.
//MTELIB_DISABLE_DGRANULE_OPERATIONS: disables usage of double-granule operations.

#if defined(MTELIB_NO_INTRINSICS) && defined(MTELIB_NO_INLINE_ASSEMBLY) 
#error Cannot disable both intrinsics and inline ASM.
#endif

#ifndef MTELIB_NO_TAG_CHECKS
    #define VERIFY_VALID_TAG(tag) MTE_ASSERT(tag <= MAX_TAG, "Invalid tag value")
#else
    #define VERIFY_VALID_TAG(...)
#endif

#ifdef MTELIB_NO_ALIGNMENT_CHECKS
    #define VERIFY_ALIGNMENT(...)
    #define VERIFY_ALIGNMENT_CRITICAL(...)

    #ifdef MTELIB_RELAXED_ALIGNMENT_CHECKS
    #error Relaxed alignment checks cannot be enabled if no alignment checks are enabled.
    #endif
#else
    #define VERIFY_ALIGNMENT_CRITICAL(d, align) MTE_ASSERT(((uint64_t)(d) & (align)) == 0, "Critical alignment constraint violation")

    #ifndef MTELIB_RELAXED_ALIGNMENT_CHECKS
        #define VERIFY_ALIGNMENT(d, align) MTE_ASSERT(((uint64_t)(d) & (align)) == 0, "Alignment constraint violation")
    #else
        #define VERIFY_ALIGNMENT(...)
    #endif
#endif

#ifndef MTELIB_NO_INTRINSICS
#include <arm_acle.h>
#endif

#define MTELIBEXPORT extern inline

/* ... */

#define MAX_TAG   (0xFULL)
#define TAG_SHIFT (56U)

#define LOG2_TAG_GRANULE_SIZE    (4U) //log2(size)
#define GRANULE_SIZE             (1U << LOG2_TAG_GRANULE_SIZE)
#define GRANULE_ALIGNMENT_MASK   (GRANULE_SIZE - 1U)

#define LOG2_DGRANULE_SIZE       (5U)
#define DGRANULE_SIZE            (1U << LOG2_DGRANULE_SIZE)
#define DGRANULE_ALIGNMENT_MASK  (DGRANULE_SIZE - 1U)

static_assert(GRANULE_SIZE == 16,  "Bad granule size");
static_assert(DGRANULE_SIZE == 32, "Bad DGRANULE size");
static_assert(GRANULE_ALIGNMENT_MASK == 0xF,   "Bad granule alignment");
static_assert(DGRANULE_ALIGNMENT_MASK == 0x1F, "Bad DGRANULE alignment");

/* Exclude mask manipulation primitives */
typedef uint64_t ExcludeMask;

MTELIBEXPORT ExcludeMask excludeMaskAddPtrTag(ExcludeMask mask, void* ptr) {
#ifndef MTELIB_NO_INTRINSICS
    return (ExcludeMask)__arm_mte_exlude_tag(ptr, mask);
#else
    ExcludeMask newMask;
	MTE_ASM("GMI %0, %1, %2" :  "=r"(newMask) : "r"(ptr), "r"(mask));
	return newMask;
#endif
}

MTELIBEXPORT ExcludeMask excludeMaskAddTag(ExcludeMask mask, uint64_t tag) {
	VERIFY_VALID_TAG(tag);
	return mask | (1 << tag);
}

MTELIBEXPORT ExcludeMask excludeMaskRemoveTag(ExcludeMask mask, uint64_t tag) {
    VERIFY_VALID_TAG(tag);
    return mask & ~(1 << tag);
}

/* Pointer tagging primitives */
MTELIBEXPORT void * pointerSetTag(void* ptr, uint64_t tag) {
	VERIFY_VALID_TAG(tag);
	return (void*)(( ((uintptr_t)ptr) & ~(MAX_TAG << TAG_SHIFT))  /* Clear existing tag */
		| (tag << TAG_SHIFT) /* insert new tag */
	);
}

MTELIBEXPORT uint64_t pointerGetTag(void* ptr) {
	return (((uintptr_t)ptr) >> TAG_SHIFT) & MAX_TAG;
}

#ifndef MTELIB_NO_INTRINSICS
    #define pointerSetRandomTag(ptr, excluded) __arm_mte_create_random_tag(ptr, excluded)
#else
MTELIBEXPORT void* pointerSetRandomTag(void* ptr, ExcludeMask excluded) {
	void* tagged;
	MTE_ASM("IRG %0, %1, %2" : "=r"(tagged) : "r"(ptr), "r"(excluded));
	return tagged;
}
#endif

/* Memory tagging/manipulation "primitives" */

/**
 * @brief Tag an area of memory
 * 
 * @param ptr Tagged pointer to area that gets tagged with the tag in ptr itself
 * @param size Size of the area to tag (Aligned to tag boundary)
 * @note If ptr isn't aligned to tag boundary, more than size bytes will be tagged.
 */
MTELIBEXPORT void memoryTag(void* ptr, size_t size) {
    VERIFY_ALIGNMENT(ptr, GRANULE_ALIGNMENT_MASK);
    VERIFY_ALIGNMENT(size, GRANULE_ALIGNMENT_MASK);

	void* const end = ((char*)ptr + size); //TODO: ensure this is using the safe pointer addition instruction

#ifndef MTELIB_NO_INLINE_ASSEMBLY
  #ifndef MTELIB_DISABLE_DGRANULE_OPERATIONS
	if ((size & DGRANULE_ALIGNMENT_MASK) != 0) {
		MTE_ASM("STG %0, [%0], #16" : "+r"(ptr));
	}

	while (ptr < end) {
		MTE_ASM("ST2G %0, [%0], #32" : "+r"(ptr) ::);
	}
  #else
    while (ptr < end) {
        MTE_ASM("STG %0, [%0], #16" : "+r"(ptr));
    }
  #endif
#else
    const char* out = (const char*)ptr;
    while (out < end) {
        __arm_mte_set_tag(out);
        out += GRANULE_SIZE;
    }
#endif
}

/**
 * @brief bzero while tagging destination area
 * 
 * @param ptr Tagged pointer to area that gets zero'ed out and tagged
 * @param size Size of the area to zero out
 * @note ptr must be aligned to tag boundary
 * @note size must be aligned to tag boundary
 */
MTELIBEXPORT void memoryTagAndZero(void* ptr, size_t size) {
    //Unlike STG, STZG aborts if the pointer is not aligned to tag granule size.
    VERIFY_ALIGNMENT_CRITICAL(ptr, GRANULE_ALIGNMENT_MASK);
    VERIFY_ALIGNMENT_CRITICAL(size, GRANULE_ALIGNMENT_MASK);

#ifndef MTELIB_NO_INLINE_ASSEMBLY
	void* const end = ((char*)ptr + size);
  #ifndef MTELIB_DISABLE_DGRANULE_OPERATIONS
	if ((size & DGRANULE_ALIGNMENT_MASK) != 0) {
		MTE_ASM("STZG %0, [%0], #16" : "+r"(ptr) :: "memory");
	}

	while (ptr < end) {
		MTE_ASM("STZ2G %0, [%0], #32" : "+r"(ptr) :: "memory");
	}
  #else
    while (ptr < end) {
        MTE_ASM("STZG %0, [%0], #16" : "+r"(ptr) :: "memory");
    }
  #endif
#else
    uint64_t* in = (uint64_t*)src;
    uint64_t* out = (uint64_t*)dst;
    uint64_t* const end = (uint64_t*)((char*)dst + size);
    while (out < end) {
        __arm_mte_set_tag(out);
        out[0] = in[0];
        out[1] = in[1];
        out += 2; in += 2;
    }
#endif
}

/**
 * @brief memcpy while tagging destination area
 * 
 * @param dst Tagged pointer to destination
 * @param src Pointer to source
 * @param size Size of the copy
 * @note dst must be aligned to tag boundary
 * @note size must be aligned to tag boundary
 */
MTELIBEXPORT void memoryTagAndCopy(void* dst, const void* src, size_t size) {
    //Same as above, STGP aborts if pointer is not aligned to tag granule size.
	VERIFY_ALIGNMENT_CRITICAL(dst, GRANULE_ALIGNMENT_MASK);
	VERIFY_ALIGNMENT_CRITICAL(size, GRANULE_ALIGNMENT_MASK);

#ifndef MTELIB_NO_INLINE_ASSEMBLY
    void* const end = ((char*)dst + size);

	//STGP only works on a single granule (no ST2GP), so we'll do granule by granule
	while (dst < end) {
		uint64_t low, high;
		MTE_ASM("LDP %0, %1, [%2], #16" : "=r"(low), "=r"(high), "+r"(src));
		MTE_ASM("STGP %[lo], %[hi], [%[ptr]], #16" : [ptr]"+r"(dst) : [lo]"r"(low), [hi]"r"(high) : "memory");
	}
#else
    uint64_t* in = (uint64_t*)src;
    uint64_t* out = (uint64_t*)dst;
    uint64_t* const end = (uint64_t*)((char*)dst + size);
    while (out < end) {
        __arm_mte_set_tag(out);
        out[0] = in[0];
        out[1] = in[1];
        out += 2; in += 2; 
    }
#endif
}