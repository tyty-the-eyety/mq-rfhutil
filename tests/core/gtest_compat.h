// gtest_compat.h - GTEST_SKIP for googletest older than 1.10.
// The Windows UnitTests project uses the googletest 1.8.1 NuGet package, which has
// no skip support; there a skipped test returns early and is reported as passed.
#pragma once
#include <gtest/gtest.h>
#ifndef GTEST_SKIP
#define GTEST_SKIP() return GTEST_SUCCEED()
#endif
