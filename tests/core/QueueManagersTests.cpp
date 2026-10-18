// QueueManagersTests.cpp - rfh::mq::listLocalQueueManagers against hand-built mqs.ini files.
//
// The fixtures follow the file IBM MQ 9.4 installs (AllQueueManagers:, LogDefaults:, QueueManager:
// with Name, Prefix, Directory, InstallationName) and IBM's documented DefaultQueueManager: stanza.
// QueueManagers.LiveMachineFile reads the real file of this machine and skips when it cannot.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "gtest_compat.h"
#include "rfh_queue_managers.h"

using namespace rfh;
using namespace rfh::mq;

namespace
{
    // A temporary data directory per test, removed afterwards.
    class QueueManagers : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            std::string pattern = ::testing::TempDir() + "rfhqm-XXXXXX";
            std::vector<char> buf(pattern.begin(), pattern.end());
            buf.push_back(0);
            ASSERT_NE(mkdtemp(buf.data()), nullptr);
            dir = buf.data();
        }

        void TearDown() override
        {
            chmod(ini().c_str(), 0600);
            unlink(ini().c_str());
            rmdir(ini().c_str());       // when a test made mqs.ini a directory
            rmdir(dir.c_str());
        }

        std::string ini() const { return dir + "/mqs.ini"; }

        void write(const std::string & bytes)
        {
            std::ofstream(ini(), std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());
        }

        Result list(LocalQueueManagers & out)
        {
            return listLocalQueueManagers(dir, out);
        }

        static bool hasWarning(const Result & r, const std::string & part)
        {
            for (size_t i = 0; i < r.warnings.size(); i++)
            {
                if (r.warnings[i].find(part) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        static bool hasNote(const LocalQueueManager & q, const std::string & part)
        {
            for (size_t i = 0; i < q.notes.size(); i++)
            {
                if (q.notes[i].find(part) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        std::string dir;
    };

    // Restores an environment variable when the test ends.
    class EnvGuard
    {
    public:
        explicit EnvGuard(const char * n) : name(n)
        {
            const char * v = getenv(n);
            had = (v != NULL);
            if (had)
            {
                old = v;
            }
        }
        ~EnvGuard()
        {
            if (had)
            {
                setenv(name.c_str(), old.c_str(), 1);
            }
            else
            {
                unsetenv(name.c_str());
            }
        }
    private:
        std::string name;
        std::string old;
        bool had = false;
    };
}

TEST_F(QueueManagers, TwoStanzas)
{
    write("AllQueueManagers:\n"
          "   DefaultPrefix=/var/mqm\n"
          "\n"
          "QueueManager:\n"
          "   Name=QM1\n"
          "   Prefix=/var/mqm\n"
          "   Directory=QM1\n"
          "   DataPath=/data/qm1\n"
          "QueueManager:\n"
          "   Name=QM2\n"
          "   Prefix=/var/mqm\n"
          "   Directory=QM2\n"
          "   InstallationName=Installation1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_TRUE(out.fileFound);
    EXPECT_FALSE(out.truncated);
    EXPECT_EQ(out.iniPath, ini());
    ASSERT_EQ(out.managers.size(), 2u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_EQ(out.managers[0].line, 4);
    ASSERT_TRUE(out.managers[0].prefix.has_value());
    EXPECT_EQ(*out.managers[0].prefix, "/var/mqm");
    ASSERT_TRUE(out.managers[0].directory.has_value());
    EXPECT_EQ(*out.managers[0].directory, "QM1");
    ASSERT_TRUE(out.managers[0].dataPath.has_value());
    EXPECT_EQ(*out.managers[0].dataPath, "/data/qm1");
    EXPECT_TRUE(out.managers[0].notes.empty());
    EXPECT_EQ(out.managers[1].name, "QM2");
    EXPECT_EQ(out.managers[1].line, 9);
    EXPECT_FALSE(out.managers[1].dataPath.has_value());
    EXPECT_TRUE(out.managers[1].notes.empty());
    EXPECT_FALSE(out.managers[0].isDefault);
    EXPECT_FALSE(out.defaultName.has_value());
}

TEST_F(QueueManagers, OtherStanzasAreIgnored)
{
    // Name= under other stanzas is not a queue manager
    write("LogDefaults:\n"
          "   LogDefaultPath=/var/mqm/log\n"
          "   Name=NOTAQM\n"
          "ExitProperties:\n"
          "   Name=ALSO.NOT\n"
          "QueueManager:\n"
          "   Name=REAL\n"
          "   Prefix=/var/mqm\n"
          "   Directory=REAL\n"
          "Service:\n"
          "   Name=SVC\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "REAL");
}

TEST_F(QueueManagers, DefaultQueueManagerStanzaMarksTheDefault)
{
    write("DefaultQueueManager:\n"
          "   Name=QM2\n"
          "QueueManager:\n   Name=QM1\n   Prefix=/p\n   Directory=QM1\n"
          "QueueManager:\n   Name=QM2\n   Prefix=/p\n   Directory=QM2\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_TRUE(out.defaultName.has_value());
    EXPECT_EQ(*out.defaultName, "QM2");
    ASSERT_EQ(out.managers.size(), 2u);
    EXPECT_FALSE(out.managers[0].isDefault);
    EXPECT_TRUE(out.managers[1].isDefault);
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(QueueManagers, DefaultNameIsComparedExactly)
{
    write("QueueManager:\n   Name=Qm1\n   Prefix=/p\n   Directory=Qm1\n"
          "DefaultQueueManager:\n   Name=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_FALSE(out.managers[0].isDefault);
    ASSERT_TRUE(out.defaultName.has_value());
    EXPECT_EQ(*out.defaultName, "QM1");
    EXPECT_TRUE(hasWarning(r, "QM1")) << "a default that matches no stanza must be reported";
}

TEST_F(QueueManagers, DefaultNamingNoStanzaIsReportedAndNothingIsDefault)
{
    write("DefaultQueueManager:\n   Name=GONE\n"
          "QueueManager:\n   Name=QM1\n   Prefix=/p\n   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_FALSE(out.managers[0].isDefault);
    EXPECT_TRUE(hasWarning(r, "GONE"));
}

TEST_F(QueueManagers, CommentsAndBlankLines)
{
    write("# a comment with Name=NOT and QueueManager:\n"
          "\n"
          "   \t \n"
          "QueueManager:\n"
          "   # Name=COMMENTED\n"
          "   Name=QM1\n"
          "#Prefix=/not/this\n"
          "   Prefix=/var/mqm\n"
          "\n"
          "   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_EQ(*out.managers[0].prefix, "/var/mqm");
    EXPECT_EQ(out.managers[0].line, 4);
}

TEST_F(QueueManagers, CrlfLineEndings)
{
    write("DefaultQueueManager:\r\n   Name=QM2\r\n"
          "QueueManager:\r\n   Name=QM1\r\n   Prefix=/var/mqm\r\n   Directory=QM1\r\n"
          "QueueManager:\r\n   Name=QM2\r\n   Prefix=/var/mqm\r\n   Directory=QM2\r\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    ASSERT_EQ(out.managers.size(), 2u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_EQ(*out.managers[0].prefix, "/var/mqm");
    EXPECT_EQ(*out.managers[0].directory, "QM1");
    EXPECT_EQ(out.managers[1].name, "QM2");
    EXPECT_TRUE(out.managers[1].isDefault);
}

TEST_F(QueueManagers, SpacesAndTabsAroundKeysValuesAndHeaders)
{
    write("  QueueManager:  \n"
          "\t Name \t=\t QM.ONE \t\n"
          "   Prefix = /var/mqm  \n"
          "   Directory=QM!ONE\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM.ONE");
    EXPECT_EQ(*out.managers[0].prefix, "/var/mqm");
    EXPECT_EQ(*out.managers[0].directory, "QM!ONE");
}

TEST_F(QueueManagers, KeywordsIgnoreCase)
{
    write("queuemanager:\n   NAME=QM1\n   prefix=/p\n   DIRECTORY=QM1\n"
          "DEFAULTQUEUEMANAGER:\n   name=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_TRUE(out.managers[0].isDefault);
}

TEST_F(QueueManagers, NoTrailingNewline)
{
    write("QueueManager:\n   Name=QM1\n   Prefix=/p\n   Directory=QM1");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(*out.managers[0].directory, "QM1");
}

TEST_F(QueueManagers, DuplicateNamesAreListedOnceWithAWarning)
{
    write("QueueManager:\n   Name=QM1\n   Prefix=/first\n   Directory=QM1\n"
          "QueueManager:\n   Name=QM2\n   Prefix=/p\n   Directory=QM2\n"
          "QueueManager:\n   Name=QM1\n   Prefix=/second\n   Directory=QM1\n"
          "QueueManager:\n   Name=qm1\n   Prefix=/p\n   Directory=qm1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 3u) << "QM1, QM2 and qm1: names are case sensitive";
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_EQ(*out.managers[0].prefix, "/first") << "the first stanza wins";
    EXPECT_EQ(out.managers[1].name, "QM2");
    EXPECT_EQ(out.managers[2].name, "qm1");
    EXPECT_TRUE(hasWarning(r, "line 9")) << "the warning names the line of the repeat";
    EXPECT_TRUE(hasWarning(r, "QM1"));
}

TEST_F(QueueManagers, MissingNameIsSkippedWithAWarning)
{
    write("QueueManager:\n   Prefix=/var/mqm\n   Directory=ORPHAN\n"
          "QueueManager:\n   Name=\n   Prefix=/var/mqm\n"
          "QueueManager:\n   Name=QM1\n   Prefix=/var/mqm\n   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_TRUE(hasWarning(r, "line 1")) << "the first headerless stanza";
    EXPECT_TRUE(hasWarning(r, "ORPHAN")) << "what the stanza did say is not lost";
    EXPECT_TRUE(hasWarning(r, "line 4")) << "an empty Name= is a missing name";
}

TEST_F(QueueManagers, RepeatedKeyFirstWinsWithAWarning)
{
    write("QueueManager:\n   Name=QM1\n   Name=QM9\n   Prefix=/a\n   Prefix=/b\n   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_EQ(*out.managers[0].prefix, "/a");
    EXPECT_TRUE(hasWarning(r, "line 3"));
    EXPECT_TRUE(hasWarning(r, "line 5"));
}

TEST_F(QueueManagers, AbsentLocationFieldsAreNoted)
{
    write("QueueManager:\n   Name=QM1\n"
          "QueueManager:\n   Name=QM2\n   Directory=QM2\n"
          "QueueManager:\n   Name=QM3\n   DataPath=/data/qm3\n"
          "QueueManager:\n   Name=QM4\n   Prefix=/p\n   Directory=QM4\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 4u);
    EXPECT_FALSE(out.managers[0].prefix.has_value());
    EXPECT_FALSE(out.managers[0].directory.has_value());
    EXPECT_TRUE(hasNote(out.managers[0], "Prefix absent"));
    EXPECT_TRUE(hasNote(out.managers[0], "Directory absent"));
    EXPECT_TRUE(hasNote(out.managers[0], "DataPath absent"));
    EXPECT_TRUE(hasNote(out.managers[1], "Prefix absent"));
    EXPECT_FALSE(hasNote(out.managers[1], "Directory absent"));
    EXPECT_TRUE(out.managers[2].notes.empty()) << "DataPath alone says where the data is";
    EXPECT_TRUE(out.managers[3].notes.empty());
}

TEST_F(QueueManagers, NameOf48CharactersIsAccepted)
{
    std::string name(48, 'A');
    write("QueueManager:\n   Name=" + name + "\n   Prefix=/p\n   Directory=X\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, name);
    EXPECT_TRUE(out.managers[0].notes.empty());
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(QueueManagers, NameOver48CharactersIsKeptWithANote)
{
    std::string name(49, 'B');
    write("QueueManager:\n   Name=" + name + "\n   Prefix=/p\n   Directory=X\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, name) << "the source value is not cut";
    EXPECT_TRUE(hasNote(out.managers[0], "48"));
    EXPECT_TRUE(hasWarning(r, "line 1"));
}

TEST_F(QueueManagers, BytesThatAreNotUtf8AreKeptAndNoted)
{
    // 0xE9 alone is Latin-1, not UTF-8
    write(std::string("QueueManager:\n   Name=QM\xE9X\n   Prefix=/var/mq\xFFm\n   Directory=QM1\n"
                      "QueueManager:\n   Name=OK\n   Prefix=/p\n   Directory=OK\n"));
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 2u);
    EXPECT_EQ(out.managers[0].name, std::string("QM\xE9X"));
    EXPECT_EQ(*out.managers[0].prefix, std::string("/var/mq\xFFm"));
    EXPECT_TRUE(hasNote(out.managers[0], "Name is not valid UTF-8"));
    EXPECT_TRUE(hasNote(out.managers[0], "Prefix is not valid UTF-8"));
    EXPECT_FALSE(hasNote(out.managers[0], "Directory is not valid UTF-8"));
    EXPECT_TRUE(out.managers[1].notes.empty());
}

TEST_F(QueueManagers, ValidMultiByteUtf8IsNotFlagged)
{
    write("QueueManager:\n   Name=QM1\n   Prefix=/var/mqm/caf\xC3\xA9\n   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_TRUE(out.managers[0].notes.empty());
}

TEST_F(QueueManagers, OverlongAndTruncatedUtf8SequencesAreFlagged)
{
    // C0 80 is an overlong NUL; E2 82 is a cut-off three byte sequence
    write(std::string("QueueManager:\n   Name=A\xC0\x80Z\n   Prefix=/p\xE2\x82\n   Directory=D\n"));
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_TRUE(hasNote(out.managers[0], "Name is not valid UTF-8"));
    EXPECT_TRUE(hasNote(out.managers[0], "Prefix is not valid UTF-8"));
}

TEST_F(QueueManagers, LineWithANulByteIsSkipped)
{
    write(std::string("QueueManager:\n   Name=QM1\n   Prefix=/p") + std::string(1, '\0') + "q\n   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_FALSE(out.managers[0].prefix.has_value()) << "the line with the NUL was not used";
    EXPECT_TRUE(hasWarning(r, "line 3"));
    EXPECT_TRUE(hasWarning(r, "NUL"));
}

TEST_F(QueueManagers, Utf8ByteOrderMarkAtTheStartIsIgnored)
{
    write("\xEF\xBB\xBFQueueManager:\n   Name=QM1\n   Prefix=/p\n   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM1");
}

TEST_F(QueueManagers, UnrecognisedLinesAreWarnedAboutAndTheRestIsUsed)
{
    write("stray=1\n"
          "this line is not a stanza or a setting\n"
          "QueueManager:\n"
          "   Name=QM1\n"
          "   garbage without equals\n"
          "   Prefix=/p\n"
          "   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_TRUE(hasWarning(r, "line 1"));
    EXPECT_TRUE(hasWarning(r, "line 2"));
    EXPECT_TRUE(hasWarning(r, "line 5"));
}

TEST_F(QueueManagers, EmptyFile)
{
    write("");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(out.fileFound);
    EXPECT_TRUE(out.managers.empty());
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(QueueManagers, FileOfExactlyTheLimitIsNotTruncated)
{
    std::string text = "QueueManager:\n   Name=QM1\n   Prefix=/p\n   Directory=QM1\n";
    text += "#" + std::string(kMaxIniBytes - text.size() - 2, 'x') + "\n";
    ASSERT_EQ(text.size(), kMaxIniBytes);
    write(text);
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_FALSE(out.truncated);
    EXPECT_TRUE(r.warnings.empty());
    ASSERT_EQ(out.managers.size(), 1u);
}

TEST_F(QueueManagers, HugeFileIsCutAtTheLimitAndSaysSo)
{
    // thousands of stanzas, about 2.5 MiB: the limit falls inside a line
    std::string text;
    int count = 0;
    while (text.size() < 2 * kMaxIniBytes + kMaxIniBytes / 2)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "QueueManager:\n   Name=QM%07d\n   Prefix=/var/mqm\n   Directory=QM%07d\n", count, count);
        text += buf;
        count++;
    }
    write(text);
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(out.truncated);
    EXPECT_TRUE(hasWarning(r, "truncated"));
    EXPECT_TRUE(hasWarning(r, std::to_string(kMaxIniBytes)));
    ASSERT_GT(out.managers.size(), 1000u);
    EXPECT_LT(out.managers.size(), (size_t)count);
    for (size_t i = 0; i < out.managers.size(); i++)
    {
        // every name is whole (QM + 7 digits): a line cut by the limit was dropped, not parsed
        ASSERT_EQ(out.managers[i].name.size(), 9u) << "entry " << i;
        char want[32];
        snprintf(want, sizeof(want), "QM%07d", (int)i);
        ASSERT_EQ(out.managers[i].name, want);
    }
    // a stanza whose last field was cut off: Directory is missing or whole, never half a name
    const LocalQueueManager & last = out.managers.back();
    if (last.directory.has_value())
    {
        EXPECT_EQ(last.directory->size(), 9u);
    }
}

TEST_F(QueueManagers, OneEnormousLineIsBoundedAndHarmless)
{
    write("QueueManager:\n   Name=QM1\n   Prefix=/p\n   Directory=QM1\n" + std::string(2 * kMaxIniBytes, 'A'));
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(out.truncated);
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "QM1");
    EXPECT_TRUE(hasWarning(r, "truncated"));
}

TEST_F(QueueManagers, WarningsAreCapped)
{
    std::string text;
    for (int i = 0; i < 500; i++)
    {
        text += "not a setting\n";
    }
    write(text);
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(r.warnings.size(), kMaxWarnings + 1) << "the cap, then one line saying how many were left out";
    EXPECT_NE(r.warnings.back().find("450"), std::string::npos) << r.warnings.back();
}

TEST_F(QueueManagers, MissingFileIsSuccessAndEmptyWithAWarning)
{
    // no write(): there is no mqs.ini
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_FALSE(out.fileFound);
    EXPECT_EQ(out.iniPath, ini());
    EXPECT_TRUE(out.managers.empty());
    EXPECT_TRUE(hasWarning(r, ini()));
}

TEST_F(QueueManagers, MissingDirectoryIsTheSameAsMissingFile)
{
    LocalQueueManagers out;
    Result r = listLocalQueueManagers(dir + "/no/such/dir", out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_FALSE(out.fileFound);
    EXPECT_TRUE(out.managers.empty());
}

TEST_F(QueueManagers, UnreadableFileFailsWithTheReason)
{
    write("QueueManager:\n   Name=QM1\n");
    ASSERT_EQ(chmod(ini().c_str(), 0), 0);
    if (access(ini().c_str(), R_OK) == 0)
    {
        GTEST_SKIP() << "this user can read a mode 000 file (root?)";
    }
    LocalQueueManagers out;
    Result r = list(out);

    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.text.find(ini()), std::string::npos) << r.text;
    EXPECT_NE(r.text.find("ermission denied"), std::string::npos) << r.text;
    EXPECT_TRUE(out.managers.empty());
    EXPECT_EQ(out.iniPath, ini());
}

TEST_F(QueueManagers, DirectoryInsteadOfFileFails)
{
    ASSERT_EQ(mkdir(ini().c_str(), 0700), 0);
    LocalQueueManagers out;
    Result r = list(out);

    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.text.find(ini()), std::string::npos) << r.text;
    EXPECT_TRUE(out.managers.empty());
}

TEST_F(QueueManagers, TrailingSlashOnTheDataPathIsHarmless)
{
    write("QueueManager:\n   Name=QM1\n   Prefix=/p\n   Directory=QM1\n");
    LocalQueueManagers out;
    Result r = listLocalQueueManagers(dir + "/", out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(out.iniPath, ini()) << "no doubled slash";
    ASSERT_EQ(out.managers.size(), 1u);
}

TEST_F(QueueManagers, DataPathDefaultsToMqDataPathThenVarMqm)
{
    EnvGuard guard("MQ_DATA_PATH");

    unsetenv("MQ_DATA_PATH");
    EXPECT_EQ(defaultMqDataPath(), "/var/mqm");

    setenv("MQ_DATA_PATH", "", 1);
    EXPECT_EQ(defaultMqDataPath(), "/var/mqm") << "an empty variable is not a path";

    setenv("MQ_DATA_PATH", dir.c_str(), 1);
    EXPECT_EQ(defaultMqDataPath(), dir);

    write("QueueManager:\n   Name=FROMENV\n   Prefix=/p\n   Directory=X\n");
    LocalQueueManagers out;
    Result r = listLocalQueueManagers("", out);     // an empty path means the default

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(out.iniPath, ini());
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "FROMENV");
}

TEST_F(QueueManagers, AnExplicitPathBeatsTheEnvironment)
{
    EnvGuard guard("MQ_DATA_PATH");
    setenv("MQ_DATA_PATH", "/nonexistent/elsewhere", 1);

    write("QueueManager:\n   Name=EXPLICIT\n   Prefix=/p\n   Directory=X\n");
    LocalQueueManagers out;
    Result r = list(out);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(out.managers.size(), 1u);
    EXPECT_EQ(out.managers[0].name, "EXPLICIT");
}

TEST(QueueManagersLive, LiveMachineFile)
{
    // The real mqs.ini of this machine: MQ_DATA_PATH or /var/mqm. What it holds depends on the machine,
    // so this checks the facts every file must satisfy and prints what it found.
    std::string path = defaultMqDataPath() + "/mqs.ini";
    if (access(path.c_str(), R_OK) != 0)
    {
        GTEST_SKIP() << "no readable " << path << " on this machine (no local IBM MQ server install)";
    }

    LocalQueueManagers out;
    Result r = listLocalQueueManagers("", out);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(out.fileFound);
    EXPECT_EQ(out.iniPath, path);
    EXPECT_FALSE(out.truncated);
    printf("[ live ] %s: %zu queue manager(s), default %s, %zu warning(s)\n", path.c_str(), out.managers.size(),
           out.defaultName ? out.defaultName->c_str() : "(none)", r.warnings.size());
    for (size_t i = 0; i < out.managers.size(); i++)
    {
        const LocalQueueManager & q = out.managers[i];
        EXPECT_FALSE(q.name.empty());
        EXPECT_LE(q.name.size(), kMaxQueueManagerNameLength);
        printf("[ live ]   %s prefix=%s directory=%s dataPath=%s default=%d line=%d notes=%zu\n", q.name.c_str(),
               q.prefix ? q.prefix->c_str() : "(absent)", q.directory ? q.directory->c_str() : "(absent)",
               q.dataPath ? q.dataPath->c_str() : "(absent)", (int)q.isDefault, q.line, q.notes.size());
    }
}
