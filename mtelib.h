/**
 * @file mtelib.h
 * @author CreepNT
 * @brief Wrapper around ASM primitives for MTE
 * 
 * @copyright Copyright (c) CreepNT 2022
 * @todo Switch to intrinsics if available
 * @todo Allow misaligned pointers/sized by compile-time defines
 */

#include <assert.h> //assert
#include <stddef.h> //size_t
#include <stdint.h> //uint64_t, uintptr_t

#include <stdio.h>

#define MAX_TAG   (0xFULL)
#define TAG_SHIFT (56U)

#define LOG2_TAG_GRANULE_SIZE    (4U) //log2(size)
#define GRANULE_SIZE             (1U << LOG2_TAG_GRANULE_SIZE)
#define GRANULE_ALIGNMENT_MASK   (GRANULE_SIZE - 1U)

#define LOG2_DGRANULE_SIZE       (5U)
#define DGRANULE_SIZE            (1U << LOG2_DGRANULE_SIZE)
#define DGRANULE_ALIGNMENT_MASK  (DGRANULE_SIZE - 1U)

static_assert(GRANULE_SIZE == 16, "Bad granule size");
static_assert(DGRANULE_SIZE == 32, "Bad DGRANULE size");
static_assert(GRANULE_ALIGNMENT_MASK == 0xF, "Bad granule alignment");
static_assert(DGRANULE_ALIGNMENT_MASK == 0x1F, "Bad DGRANULE alignment");

#define MTE_ASSERT(condition, errormsg) assert(condition && __FILE__ && __LINE__ && errormsg)

#define ASSERT_VALID_TAG(tag) MTE_ASSERT((tag) <= MAX_TAG, "Invalid tag")
#define ASSERT_ALIGNMENT(data, align_mask) MTE_ASSERT(((uintptr_t)(data) & (align_mask)) == 0, "Misaligned pointer")

typedef uint64_t ExcludeMask;

extern inline ExcludeMask excludeMaskAddPtrTag(ExcludeMask mask, void* ptr) {
	ExcludeMask newMask;
	__asm__ volatile("GMI %0, %1, %2" :  "=r"(newMask) : "r"(ptr), "r"(mask));
	return newMask;
}

extern inline ExcludeMask excludeMaskAddTag(ExcludeMask mask, uint64_t tag) {
	ASSERT_VALID_TAG(tag);
	return (mask | (1 << tag));
}

/* Pointer tagging primitives */
extern inline void * pointerSetTag(void* ptr, uint64_t tag) {
	ASSERT_VALID_TAG(tag);
	return (void*)( ( ((uintptr_t)ptr) & ~(MAX_TAG << TAG_SHIFT))  /* Clear existing tag */
		| (tag << TAG_SHIFT) /* insert new tag */
	);
}

extern inline uint64_t pointerGetTag(void* ptr) {
	return (((uintptr_t)ptr) >> TAG_SHIFT) & MAX_TAG;
}

extern inline void* pointerSetRandomTag(void* ptr, ExcludeMask excluded) {
	void* tagged;
	__asm__ volatile("IRG %0, %1, %2" : "=r"(tagged) : "r"(ptr), "r"(excluded));
	return tagged;
}

/* Memory tagging/manipulation "primitives" */

/**
 * @brief Tag an area of memory
 * 
 * @param ptr Tagged pointer to area that gets tagged with the tag in ptr itself
 * @param size Size of the area to tag
 * @note dst must be aligned to tag boundary
 * @note size must be aligned to tag boundary
 */
extern inline void memoryTag(void* ptr, size_t size) {
	//ptr doesn't need to be aligned, but it's better
	//(else we tag more than size bytes)

	//optimization idea: calculate ptr + size and compare against that
	//this way we don't need size -=

	void* const end = ((char*)ptr + size);

	CRP_ASSERT((size & GRANULE_ALIGNMENT_MASK) == 0, "Misaligned size");
	
	if ((size & DGRANULE_ALIGNMENT_MASK) != 0) {
		__asm__ volatile("STG %0, [%0], #16" : "+r"(ptr));
		size -= GRANULE_SIZE;
	}

	CRP_ASSERT((size & DGRANULE_ALIGNMENT_MASK) == 0, "wtf? misaligned size");

	while (ptr < end) {
		__asm__ volatile("ST2G %0, [%0], #32" : "+r"(ptr) ::);
	}
}

/**
 * @brief bzero while tagging destination area
 * 
 * @param ptr Tagged pointer to area that gets zero'ed out and tagged
 * @param size Size of the area to zero out
 * @note ptr must be aligned to tag boundary
 * @note size must be aligned to tag boundary
 */
extern inline void memoryTagAndZero(void* ptr, size_t size) {
	CRP_ASSERT((((uintptr_t)ptr) & GRANULE_ALIGNMENT_MASK) == 0, "Misaligned pointer");
	CRP_ASSERT((size & GRANULE_ALIGNMENT_MASK) == 0, "Misaligned size");

	void* const end = ((char*)ptr + size);

	//STZ2G doesn't need DGRANULE alignment, only GRANULE alignment
	//so use same logic as in memoryTag
	if ((size & DGRANULE_ALIGNMENT_MASK) != 0) {
		__asm__ volatile("STZG %0, [%0], #16" : "+r"(ptr) :: "memory" );
		size -= GRANULE_SIZE;
	}

	CRP_ASSERT((size & DGRANULE_ALIGNMENT_MASK) == 0, "wtf? misaligned size");

	while (ptr < end) {
		__asm__ volatile("STZ2G %0, [%0], #32" : "+r"(ptr) :: "memory" );
		//size -= DGRANULE_SIZE;
	}
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
extern inline void memoryTagAndCopy(void* dst, const void* src, size_t size) {
	CRP_ASSERT(((uintptr_t)dst & GRANULE_ALIGNMENT_MASK) == 0, "Misaligned pointer");
	CRP_ASSERT((size & GRANULE_ALIGNMENT_MASK) == 0, "Misaligned size");

	void* const end = ((char*)dst + size);

	//STGP only works on a single granule (no ST2GP), so we'll do granule by granule
	while (dst < end) {
		uint64_t low, high;
		__asm__ volatile("LDP %0, %1, [%2], #16" : "=r"(low), "=r"(high), "+r"(src));
		__asm__ volatile("STGP %[lo], %[hi], [%[ptr]], #16" : [ptr]"+r"(dst) : [lo]"r"(low), [hi]"r"(high) : "memory");
		//size -= GRANULE_SIZE;
	}
}
