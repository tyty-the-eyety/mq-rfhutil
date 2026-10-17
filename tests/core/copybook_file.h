// copybook_file.h - a copy book in a file of its own, for the COBOL view tests.
#pragma once

#include <gtest/gtest.h>

#include <stdio.h>
#include <unistd.h>

#include <string>

// A copy book file, removed when the object goes. ctest runs each test in a
// process of its own, in parallel, and the memcheck tests run them again, so the
// name has the test's name, the process and a sequence number.
class CopybookFile
{
public:
    CopybookFile()
    {
        static int sequence = 0;
        const testing::TestInfo * test = testing::UnitTest::GetInstance()->current_test_info();
        path_ = testing::TempDir() + "rfhutil_" + test->test_suite_name() + "." + test->name() + "."
            + std::to_string((long)getpid()) + "." + std::to_string(++sequence) + ".cpy";
    }

    explicit CopybookFile(const std::string & text) : CopybookFile()
    {
        write(text);
    }

    ~CopybookFile()
    {
        remove(path_.c_str());
    }

    CopybookFile(const CopybookFile &) = delete;
    CopybookFile & operator=(const CopybookFile &) = delete;

    // replaces the file's contents
    void write(const std::string & text)
    {
        FILE * f = fopen(path_.c_str(), "wb");
        ASSERT_NE(nullptr, f) << path_;
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }

    const std::string & path() const { return path_; }

private:
    std::string path_;
};
