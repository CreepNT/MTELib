
#include "mtelib.h"

#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/mman.h>
#include <sys/prctl.h>

void SIGSEGV_handler(int signal, siginfo_t* si, void* arg) {
	puts("\n~~SIGSEGV signal handler called~~");	
	if (si->si_code == SEGV_MTESERR) {
		printf("si_code == SEGV_MTESERR (MTE Synchronous Error)\n");
		printf("si_addr = %p\n", si->si_addr);
	}
	//Maybe we should munmap() here? Process will die anyways so I guess it doesn't matter much
	exit(0);
}

int main(void) {
	puts("== MTE test application ==");

	int curTagCtrl = prctl(PR_GET_TAGGED_ADDR_CTRL, 0, 0, 0, 0);
	printf("prctl(PR_GET_TAGGED_ADDR_CTRL) -> %#x\n", curTagCtrl);

	//Enable MTE for syscalls + synchronous exception dispatching
	int res = prctl(PR_SET_TAGGED_ADDR_CTRL,
		PR_TAGGED_ADDR_ENABLE | PR_MTE_TCF_SYNC | ((0x7FF8 & ~(1 << MAX_TAG)) << 3), 0, 0, 0);
	//shl by 3 to be in PR_MTE_TAG_MASK (this takes an INCLUDE mask so remove MAX_TAG - it is reserved)

	printf("prctl(PR_SET_TAGGED_ADDR_CTRL) -> %d\n", res);
	if (res < 0) {
	        printf("Error %d: %s\n", res, strerror(res));
		return 1;
	}

	struct sigaction sa = {0};
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = SIGSEGV_handler;
	sa.sa_flags = SA_SIGINFO;
	res = sigaction(SIGSEGV, &sa, NULL);
	printf("sigaction(SIGSEGV) -> %d\n", res);
	if (res == -1) {
		printf("Error %d: %s\n", errno, strerror(errno));
		return 1;
	}

	void* mem = mmap(NULL, 0x1000, PROT_MTE | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	printf("mmap() -> %p\n", mem);
	if (mem == MAP_FAILED) {
		printf("Error %d: %s\n", errno, strerror(errno));
		return 1;
	}

	uint64_t* ptr = (uint64_t*)mem;
	printf("Pointer tag from mmap(): %ld\n", pointerGetTag(ptr));

	puts("\n== memoryTag test ==\n");

	puts("Generating random pointer tag...");
	ptr = pointerSetRandomTag(ptr, 0);
	printf("Pointer tag is now %ld\n", pointerGetTag(ptr));
	
	puts("Tagging memory using pointer...");
	memoryTag(ptr, 0x1000);
	puts("Memory tagged.");

	puts("Writing 2 to start of memory block through our tagged pointer...");
	*ptr = 2;
	printf("Value in memory = %#lx\n", *ptr);

	puts("\n== memoryTagAndZero test ==\n");

	puts("Generating new different and random tag...");
	ptr = pointerSetRandomTag(ptr, excludeMaskAddPtrTag(0, ptr));
	printf("New tag is %ld\n", pointerGetTag(ptr));
	puts("Zero+tagging...");
	memoryTagAndZero(ptr, 0x1000);
	puts("Checking memory is zero'ed...:");
	printf("*ptr = %#lx\n", *ptr);
	printf("ptr[511] = %#lx\n", ptr[511]);

	puts("\n== memoryTagAndCopy test ==\n");
	*ptr = 0x536f6d7546676942UL;
	ptr[1] = 0x6f6c6c6548737961UL;
	ptr[2] = 0UL;
	printf("Memory block start (%p) now contains string '%s'.\n", ptr, (char*)ptr);
	
	char* dst = ((char*)mem) + 64;
	dst = pointerSetRandomTag(dst, excludeMaskAddPtrTag(0, ptr));
	printf("Copy+tagging to %p (randomly generated tag=%ld)\n", dst, pointerGetTag(dst));
	memoryTagAndCopy(dst, ptr, 64);
	printf("Data at %p: '%s'\n", dst, dst);

	puts("\n== Exclude masks test ==\n");
	uint64_t notAllowedTag = pointerGetTag(dst);
	printf("Reusing our previously tagged pointer %p (tag %ld)\n", dst, notAllowedTag);
	printf("Doing 1000 rounds of random tag generation with tag %ld excluded...", notAllowedTag);
	
	ExcludeMask excludeMask = excludeMaskAddTag(0, notAllowedTag);
	
	bool gotBadTag = false;
	for (int i = 0; i < 1000; i++) {
		char* randomTagged = pointerSetRandomTag(dst, excludeMask);
		uint64_t genedTag = pointerGetTag(randomTagged);
		if (genedTag == notAllowedTag) {
			//This should never happen
			printf("!!! Got not allowed tag %ld !!!\n", pointerGetTag(randomTagged));
			gotBadTag = true;
			break;
		}
		if (genedTag == MAX_TAG) {
			//and neither should this
			printf("??? Got tag 15 ???\n");
		}
	}
	if (!gotBadTag) {
		printf("Never got tag %ld :D\n", notAllowedTag);
	}

	puts("\n== MTE violations test ==\n");
	uint64_t* mteViolator = (uint64_t*)pointerSetTag(ptr, MAX_TAG);
	puts("Using tag 15, excluded from random generation via prctl().");
	printf(" badPtr = %p\n", mteViolator);
	printf("*badPtr = \n");
	printf("%ld\n", *mteViolator); //SIGSEGV here
	printf("Survived illegal access?\n");

	munmap(mem, 0x1000);
	return 0;
}
