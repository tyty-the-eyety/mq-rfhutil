// MqLoaderTests.cpp — tests for rfh::MqLoader.
// The tests do not depend on their order: the failure case uses the uncached
// detail::loadFrom, and only the tests of the cached MqLoader::load share state.
//
// Tests skip when IBM MQ is not installed. Set RFHUTIL_REQUIRE_MQ=1 to make those
// skips failures (for CI machines that must have MQ).

#include <gtest/gtest.h>

#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "rfh_mq_loader.h"
#include "rfh_mq_loader_detail.h"

using namespace rfh;

namespace
{
    bool requireMq()
    {
        const char * v = getenv("RFHUTIL_REQUIRE_MQ");
        return v != nullptr && std::string(v) == "1";
    }
}

// Skips, or fails when RFHUTIL_REQUIRE_MQ=1, because MQ is not available.
#define SKIP_OR_FAIL_NO_MQ(message) \
    do { if (requireMq()) { FAIL() << message; } else { GTEST_SKIP() << message; } } while (0)

TEST(MqLoader, MissingInstallationReportsEveryPathTried)
{
    MqApi api;
    Result r = detail::loadFrom(detail::libraryCandidates("/nonexistent/mqm"), api);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("IBM MQ library not found"));
    EXPECT_NE(std::string::npos, r.text.find("/nonexistent/mqm/lib64/libmqm_r.so"));
    EXPECT_NE(std::string::npos, r.text.find("/nonexistent/mqm/lib64/libmqic_r.so"));
    EXPECT_EQ(nullptr, api.connx);
}

#ifndef _WIN32
TEST(MqLoader, CandidatesListServerLibraryThenClientLibrary)
{
    std::vector<detail::LibraryCandidate> list = detail::libraryCandidates("/x");
    ASSERT_EQ(2u, list.size());
    EXPECT_EQ("/x/lib64/libmqm_r.so", list[0].path);
    EXPECT_TRUE(list[0].serverLibrary);
    EXPECT_EQ("/x/lib64/libmqic_r.so", list[1].path);
    EXPECT_FALSE(list[1].serverLibrary);
    EXPECT_FALSE(list[0].bindingsAvailable) << "no queue manager under /x";
}
#endif

TEST(MqLoader, LoadsServerLibraryWithAllEntryPoints)
{
    MqApi api;
    Result r = MqLoader::load(api);
    if (!r.ok())
    {
        SKIP_OR_FAIL_NO_MQ(r.text);   // no IBM MQ on this machine
    }

    EXPECT_NE(nullptr, api.connx);
    EXPECT_NE(nullptr, api.disc);
    EXPECT_NE(nullptr, api.open);
    EXPECT_NE(nullptr, api.close);
    EXPECT_NE(nullptr, api.put);
    EXPECT_NE(nullptr, api.get);
    EXPECT_NE(nullptr, api.inq);
    EXPECT_FALSE(api.libraryPath.empty());
    EXPECT_EQ(api.libraryPath.find("libmqm_r") != std::string::npos, api.serverLibrary);

#ifndef _WIN32
    if (api.libraryPath.compare(0, 9, "/opt/mqm/") == 0)
    {
        // existence, not X_OK: strmqm is executable only by the mqm group, yet any
        // authorised user can connect in bindings mode
        EXPECT_EQ(api.serverLibrary && access("/opt/mqm/bin/strmqm", F_OK) == 0, api.bindingsAvailable);
    }
#endif
}

TEST(MqLoader, LaterCallsReturnTheFirstLibrary)
{
    MqApi first;
    MqApi second;
    if (!MqLoader::load(first).ok())
    {
        SKIP_OR_FAIL_NO_MQ("no IBM MQ on this machine");
    }

    ASSERT_TRUE(MqLoader::load(second, "/nonexistent/mqm").ok());
    EXPECT_EQ(first.libraryPath, second.libraryPath);
    EXPECT_EQ(first.connx, second.connx);
}
