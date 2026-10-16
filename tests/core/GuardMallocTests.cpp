// GuardMallocTests.cpp - checks that the memcheck tests really are guarded.
//
// Run normally, these tests skip. The memcheck run (LD_PRELOAD of the guard
// allocator, see tests/CMakeLists.txt) proves an overrun faults, so a memcheck
// test that passes means the code under test stayed within its buffers.

#include <gtest/gtest.h>

#include <stdlib.h>
#include <string.h>

namespace
{
    bool guarded()
    {
        const char * preload = getenv("LD_PRELOAD");
        return (preload != NULL) && (strstr(preload, "guardmalloc") != NULL);
    }
}

TEST(GuardMalloc, WritePastTheEndFaults)
{
    if (!guarded())
    {
        GTEST_SKIP() << "runs only under the guard allocator (ctest -L memcheck)";
    }
    EXPECT_DEATH({
        volatile char * p = (volatile char *)malloc(16);
        p[16] = 'x';
    }, "");
}

TEST(GuardMalloc, ReadPastTheEndFaults)
{
    if (!guarded())
    {
        GTEST_SKIP() << "runs only under the guard allocator (ctest -L memcheck)";
    }
    EXPECT_DEATH({
        volatile char * p = (volatile char *)malloc(32);
        volatile char c = p[32];
        (void)c;
    }, "");
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
