// MqLoaderTests.cpp — tests for rfh::MqLoader.
// Its own executable because the loader keeps the first library it loads for
// the life of the process; the failure case must run before any success.

#include <gtest/gtest.h>

#include "rfh_mq_loader.h"

using namespace rfh;

TEST(MqLoader, MissingInstallationReportsEveryPathTried)
{
    MqApi api;
    Result r = MqLoader::load(api, "/nonexistent/mqm");
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("IBM MQ library not found"));
    EXPECT_NE(std::string::npos, r.text.find("/nonexistent/mqm/lib64/libmqm_r.so"));
    EXPECT_NE(std::string::npos, r.text.find("/nonexistent/mqm/lib64/libmqic_r.so"));
    EXPECT_EQ(nullptr, api.connx);
}

TEST(MqLoader, LoadsServerLibraryWithAllEntryPoints)
{
    MqApi api;
    Result r = MqLoader::load(api);
    if (!r.ok())
    {
        GTEST_SKIP() << r.text;   // no IBM MQ on this machine
    }

    EXPECT_NE(nullptr, api.connx);
    EXPECT_NE(nullptr, api.disc);
    EXPECT_NE(nullptr, api.open);
    EXPECT_NE(nullptr, api.close);
    EXPECT_NE(nullptr, api.put);
    EXPECT_NE(nullptr, api.get);
    EXPECT_NE(nullptr, api.inq);
    EXPECT_FALSE(api.libraryPath.empty());
    EXPECT_EQ(api.libraryPath.find("libmqm_r") != std::string::npos, api.supportsBindings);
}

TEST(MqLoader, LaterCallsReturnTheFirstLibrary)
{
    MqApi first;
    MqApi second;
    if (!MqLoader::load(first).ok())
    {
        GTEST_SKIP() << "no IBM MQ on this machine";
    }

    ASSERT_TRUE(MqLoader::load(second, "/nonexistent/mqm").ok());
    EXPECT_EQ(first.libraryPath, second.libraryPath);
    EXPECT_EQ(first.connx, second.connx);
}
