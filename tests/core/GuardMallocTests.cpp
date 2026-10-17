// GuardMallocTests.cpp - checks that the memcheck tests really are guarded.
//
// Run normally, the tests that need the guard allocator skip. The memcheck run
// (LD_PRELOAD of the guard allocator, see tests/CMakeLists.txt) proves an
// overrun faults, so a memcheck test that passes means the code under test did
// not overrun a block by 16 bytes or more.

#include <gtest/gtest.h>

#include <dlfcn.h>
#include <errno.h>
#include <malloc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    bool guarded()
    {
        return dlsym(RTLD_DEFAULT, "rfh_guardmalloc_active") != NULL;
    }

    // Report that the block was usable, so a death test fails if the fault came
    // from anywhere but the access after it.
    void inBoundsOk()
    {
        fputs("in bounds ok\n", stderr);
        fflush(stderr);
    }

    bool aligned(void * p, size_t alignment)
    {
        return ((uintptr_t)p % alignment) == 0;
    }
}

#define REQUIRE_GUARDED() \
    if (!guarded()) \
    { \
        GTEST_SKIP() << "runs only under the guard allocator (ctest -L memcheck)"; \
    }

TEST(GuardMalloc, WritePastTheEndFaults)
{
    REQUIRE_GUARDED();
    EXPECT_EXIT({
        volatile char * p = (volatile char *)malloc(16);
        if (p == NULL) abort();
        p[0] = 'a';
        p[15] = 'b';
        inBoundsOk();
        p[16] = 'x';
        _exit(0);
    }, ::testing::KilledBySignal(SIGSEGV), "in bounds ok");
}

TEST(GuardMalloc, ReadPastTheEndFaults)
{
    REQUIRE_GUARDED();
    EXPECT_EXIT({
        volatile char * p = (volatile char *)malloc(32);
        if (p == NULL) abort();
        p[0] = 'a';
        p[31] = 'b';
        inBoundsOk();
        volatile char c = p[32];
        (void)c;
        _exit(0);
    }, ::testing::KilledBySignal(SIGSEGV), "in bounds ok");
}

TEST(GuardMalloc, UseAfterFreeFaults)
{
    REQUIRE_GUARDED();
    EXPECT_EXIT({
        volatile char * p = (volatile char *)malloc(64);
        if (p == NULL) abort();
        p[0] = 'a';
        p[63] = 'b';
        inBoundsOk();
        free((void *)p);
        p[0] = 'x';
        _exit(0);
    }, ::testing::KilledBySignal(SIGSEGV), "in bounds ok");
}

TEST(GuardMalloc, BlocksWithinTheirSizeAreUsable)
{
    char * p = (char *)malloc(4096);
    ASSERT_NE(nullptr, p);
    memset(p, 'a', 4096);
    char * q = (char *)realloc(p, 8192);
    ASSERT_NE(nullptr, q);
    EXPECT_EQ('a', q[4095]);
    free(q);

    int * zeros = (int *)calloc(100, sizeof(int));
    ASSERT_NE(nullptr, zeros);
    EXPECT_EQ(0, zeros[99]);
    free(zeros);
}

TEST(GuardMalloc, HugeSizesFailInsteadOfWrapping)
{
    REQUIRE_GUARDED();
    volatile size_t huge = SIZE_MAX;
    volatile size_t nearly = SIZE_MAX - 4000;
    volatile size_t eight = SIZE_MAX - 8;
    EXPECT_EQ(nullptr, malloc(huge));
    EXPECT_EQ(nullptr, malloc(nearly));
    EXPECT_EQ(nullptr, calloc(1, eight));
    EXPECT_EQ(nullptr, realloc(NULL, huge));
}

TEST(GuardMalloc, ReallocToZeroFreesTheBlock)
{
    void * p = malloc(10);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(nullptr, realloc(p, 0));
}

TEST(GuardMalloc, ReallocKeepsTheContents)
{
    char * p = (char *)malloc(10);
    ASSERT_NE(nullptr, p);
    memcpy(p, "123456789", 10);
    char * q = (char *)realloc(p, 100);
    ASSERT_NE(nullptr, q);
    EXPECT_STREQ("123456789", q);
    free(q);
}

TEST(GuardMalloc, PosixMemalignHonoursTheAlignment)
{
    size_t alignments[] = { 64, 4096 };
    for (size_t alignment : alignments)
    {
        void * p = NULL;
        ASSERT_EQ(0, posix_memalign(&p, alignment, 100));
        ASSERT_NE(nullptr, p);
        EXPECT_TRUE(aligned(p, alignment)) << alignment;
        memset(p, 'a', 100);
        free(p);
    }
}

TEST(GuardMalloc, MemalignHonoursTheAlignment)
{
    size_t alignments[] = { 64, 4096 };
    for (size_t alignment : alignments)
    {
        void * p = memalign(alignment, 100);
        ASSERT_NE(nullptr, p);
        EXPECT_TRUE(aligned(p, alignment)) << alignment;
        memset(p, 'a', 100);
        free(p);
    }
}

TEST(GuardMalloc, PosixMemalignRejectsBadAlignments)
{
    void * p = (void *)0x1234;
    EXPECT_EQ(EINVAL, posix_memalign(&p, 24, 8));       // not a power of two
    EXPECT_EQ(EINVAL, posix_memalign(&p, 4, 8));        // not a multiple of sizeof(void *)
    EXPECT_EQ(EINVAL, posix_memalign(&p, 0, 8));
    EXPECT_EQ((void *)0x1234, p);                       // untouched on failure
}

TEST(GuardMalloc, MemalignRejectsAnAlignmentThatIsNotAPowerOfTwo)
{
    REQUIRE_GUARDED();
    volatile size_t alignment = 24;
    EXPECT_EQ(nullptr, memalign(alignment, 8));
    EXPECT_EQ(EINVAL, errno);
}

TEST(GuardMalloc, VallocAndPvallocAlignToPages)
{
    long page = sysconf(_SC_PAGESIZE);
    void * v = valloc(10);
    ASSERT_NE(nullptr, v);
    EXPECT_TRUE(aligned(v, (size_t)page));
    free(v);

    void * pv = pvalloc(10);
    ASSERT_NE(nullptr, pv);
    EXPECT_TRUE(aligned(pv, (size_t)page));
    EXPECT_GE(malloc_usable_size(pv), (size_t)page);
    free(pv);
}

TEST(GuardMalloc, ForeignPointersAbortLoudly)
{
    REQUIRE_GUARDED();
    EXPECT_EXIT({
        static char foreign[256];
        // through a volatile pointer, so the compiler does not warn that this is
        // not a heap pointer: that is what the test is about
        void * volatile notFromMalloc = foreign + 128;
        free(notFromMalloc);
        _exit(0);
    }, ::testing::KilledBySignal(SIGABRT), "guardmalloc");
}
