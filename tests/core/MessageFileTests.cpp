// MessageFileTests.cpp — tests for rfh::fileio (ports of DataArea::ReadFileData and
// DataArea::WriteFile). The golden files are built with mq_layout.h at IBM's
// documented offsets, independent of the code under test.

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <cmqc.h>

#include "rfh_codepage.h"
#include "rfhconst.h"
#include "rfh_message_file.h"
#include "mq_layout.h"

using namespace rfh;
using mq_layout::Order;
namespace L = mq_layout;
namespace O = mq_layout::mqmd;

namespace
{
	class MessageFile : public ::testing::Test
	{
	protected:
		std::string path(const char * name)
		{
			return ::testing::TempDir() + "rfhutil-" + name;
		}

		std::string save(const char * name, const std::vector<unsigned char> & bytes)
		{
			std::string p = path(name);
			std::ofstream(p, std::ios::binary).write((const char *)bytes.data(), (std::streamsize)bytes.size());
			return p;
		}

		std::vector<unsigned char> load(const std::string & p)
		{
			std::ifstream in(p, std::ios::binary);
			return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		fileio::ReadOptions options()
		{
			fileio::ReadOptions o;
			o.ansiCcsid = 1252;
			return o;
		}

		// MQMD v2 for a string message, as Windows RFHUtil writes it for the tab values
		// below (persistence "No", PC encodings).
		L::Bytes stringMqmd(const char * format = "MQSTR")
		{
			L::Bytes b = L::defaultMqmd2();
			b.putLong(O::CodedCharSetId, 1208);
			b.putChars(O::Format, 8, format);
			b.putLong(O::Persistence, 0);
			b.putLong(O::Priority, 0);
			b.putLong(O::Expiry, -1);
			b.putChars(O::PutDate, 8, "20261016");
			b.putChars(O::PutTime, 8, "10111213");
			return b;
		}
	};
}

TEST_F(MessageFile, PlainTextIsAllBody)
{
	std::string p = save("plain.txt", L::text("hello"));
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(p, options(), f, r);

	ASSERT_TRUE(res.ok()) << res.text;
	EXPECT_EQ("5 bytes read from file " + p, res.text);
	EXPECT_FALSE(r.hadMqmd);
	EXPECT_TRUE(r.message.headers.empty());
	EXPECT_EQ(L::text("hello"), r.message.body);
	EXPECT_EQ(1252, r.fileCcsid);
	EXPECT_EQ(CHAR_ASCII, r.charFormat);
}

TEST_F(MessageFile, MissingFileReportsItsName)
{
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(path("does-not-exist"), options(), f, r);
	EXPECT_FALSE(res.ok());
	EXPECT_EQ("Could not open file " + path("does-not-exist"), res.text);
}

TEST_F(MessageFile, MqmdIsStrippedAndFillsTheTab)
{
	auto bytes = L::join({ stringMqmd().data, L::text("body") });
	std::string p = save("mqmd.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(p, options(), f, r);

	ASSERT_TRUE(res.ok());
	EXPECT_EQ("4 bytes read from file " + p, res.text) << "Windows does not count the MQMD";
	EXPECT_TRUE(r.hadMqmd);
	EXPECT_EQ(L::text("body"), r.message.body);
	EXPECT_EQ("MQSTR", f.format);
	EXPECT_EQ("1208", f.ccsid);
	EXPECT_EQ("2026/10/16 10:11:12.13", f.dateTime);
	EXPECT_EQ(1208, r.fileCcsid);
}

TEST_F(MessageFile, Version1MqmdStrips324Bytes)
{
	L::Bytes md = stringMqmd();
	md.putLong(4, 1);
	std::vector<unsigned char> v1(md.data.begin(), md.data.begin() + O::Length1);
	auto bytes = L::join({ v1, L::text(std::string(60, 'x')) });
	std::string p = save("v1.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_TRUE(r.hadMqmd);
	EXPECT_EQ(60u, r.message.body.size());
}

TEST_F(MessageFile, ShortVersion1MqmdIsNotRecognisedHistoric)
{
	// Windows requires sizeof(MQMD2) bytes even for a version 1 MQMD.
	L::Bytes md = stringMqmd();
	md.putLong(4, 1);
	std::vector<unsigned char> v1(md.data.begin(), md.data.begin() + O::Length1);
	auto bytes = L::join({ v1, L::text("abc") });
	std::string p = save("v1short.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_FALSE(r.hadMqmd);
	EXPECT_EQ(bytes.size(), r.message.body.size());
}

TEST_F(MessageFile, BigEndianMqmdIsNormalisedAndIdsKept)
{
	L::Bytes md = L::defaultMqmd2(Order::Big);
	md.putLong(O::CodedCharSetId, 500, Order::Big);
	md.putLong(O::Encoding, 785, Order::Big);
	md.putChars(O::Format, 8, "MQSTR");
	md.putBytes(O::MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24);
	std::string p = save("big.msg", L::join({ md.data, L::text("X") }));
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	ASSERT_TRUE(r.hadMqmd);
	EXPECT_EQ("500", f.ccsid);
	EXPECT_EQ(NUMERIC_HOST, f.encoding);
	EXPECT_EQ(0, memcmp(f.msgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24));
	EXPECT_EQ(CHAR_EBCDIC, r.charFormat);
}

TEST_F(MessageFile, MqmdThenRfh2ThenBody)
{
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
	auto bytes = L::join({ stringMqmd("MQHRF2").data, rfh.data, L::text("body") });
	std::string p = save("rfh2.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(p, options(), f, r);

	ASSERT_TRUE(res.ok());
	EXPECT_EQ("40 bytes read (data 4 dlq 0 rfh1 0, rfh2 36 cics 0 ims 0) from file " + p, res.text);
	EXPECT_EQ(rfh.data, r.message.headers);
	EXPECT_EQ(L::text("body"), r.message.body);
	ASSERT_EQ(1u, r.chain.headers.size());
	EXPECT_EQ("MQSTR", r.chain.bodyFormat);
}

TEST_F(MessageFile, LeadingRfh2WithoutMqmdIsDetected)
{
	auto rfh = L::rfh2(Order::Little, 546, 819, "MQSTR");
	std::string p = save("lead.msg", L::join({ rfh.data, L::text("data") }));
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_FALSE(r.hadMqmd);
	EXPECT_EQ("MQHRF2", f.format);
	EXPECT_EQ(rfh.data, r.message.headers);
	EXPECT_EQ(L::text("data"), r.message.body);
}

TEST_F(MessageFile, IgnoreHeadersKeepsThemInTheBody)
{
	auto rfh = L::rfh2(Order::Little, 546, 819, "MQSTR");
	auto bytes = L::join({ rfh.data, L::text("data") });
	std::string p = save("ignore.msg", bytes);
	fileio::ReadOptions o = options();
	o.ignoreHeaders = true;
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, o, f, r).ok());
	EXPECT_TRUE(r.message.headers.empty());
	EXPECT_EQ(bytes, r.message.body);
}

TEST_F(MessageFile, EbcdicTextIsRecognised)
{
	std::vector<unsigned char> bytes;
	for (int i = 0; i < 20; i++)
	{
		auto w = L::ebcdic("HELLO WORLD ");
		bytes.insert(bytes.end(), w.begin(), w.end());
	}
	std::string p = save("ebcdic.txt", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_EQ("500", f.ccsid);
	EXPECT_EQ(NUMERIC_HOST, f.encoding);
	EXPECT_EQ(CHAR_EBCDIC, r.charFormat);
}

TEST_F(MessageFile, FileCodePageSettingDisablesEbcdicGuess)
{
	std::vector<unsigned char> bytes;
	for (int i = 0; i < 20; i++)
	{
		auto w = L::ebcdic("HELLO WORLD ");
		bytes.insert(bytes.end(), w.begin(), w.end());
	}
	std::string p = save("ebcdic2.txt", bytes);
	fileio::ReadOptions o = options();
	o.fileCcsid = 819;
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, o, f, r).ok());
	EXPECT_EQ(819, r.fileCcsid);
	EXPECT_EQ(CHAR_ASCII, r.charFormat);
}

TEST_F(MessageFile, BigEndianByteOrderMarkSelectsHostEncoding)
{
	std::string p = save("bom.txt", { 0xFE, 0xFF, 0x00, 'A' });
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_EQ(NUMERIC_HOST, r.fileIntFormat);
	EXPECT_EQ(NUMERIC_PC, r.numericFormat) << "the display encoding comes from the header walk";
}

TEST_F(MessageFile, LineEndOptions)
{
	std::string p = save("lines.txt", L::text("a\nb\r\nc"));
	MqmdFields f;
	fileio::ReadResult r;

	fileio::ReadOptions noCrLf = options();
	noCrLf.removeCrLf = true;
	ASSERT_TRUE(fileio::read(p, noCrLf, f, r).ok());
	EXPECT_EQ(L::text("abc"), r.message.body);

	fileio::ReadOptions unix = options();
	unix.unixLineEnds = true;
	ASSERT_TRUE(fileio::read(p, unix, f, r).ok());
	EXPECT_EQ(L::text("a\r\nb\r\nc"), r.message.body);
}

TEST_F(MessageFile, ReadThenWriteIsByteIdentical)
{
	// The compatibility guarantee: a file Windows wrote, read into the MQMD tab and
	// written back with its MQMD rebuilt from the tab, is unchanged.
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
	auto golden = L::join({ stringMqmd("MQHRF2").data, rfh.data, L::text("round trip") });
	std::string in = save("golden.msg", golden);

	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(in, options(), f, r).ok());

	// r.message.mqmd is the MQMD the tab holds, so it is written directly
	const Message & out = r.message;

	fileio::WriteOptions w;
	w.includeMqmd = true;
	std::string outPath = path("golden-out.msg");
	Result res = fileio::write(outPath, out, w);
	ASSERT_TRUE(res.ok()) << res.text;
	EXPECT_EQ("410 bytes (data 10 mqmd 364 rfh2 36) written to file " + outPath, res.text);
	EXPECT_EQ(golden, load(outPath));
}

TEST_F(MessageFile, WriteDataOnlyLeavesOutHeaders)
{
	Message m;
	memcpy(m.mqmd.Format, "MQHRF2  ", 8);
	m.headers = L::rfh2(Order::Little, 546, 1208, "MQSTR").data;
	m.body = L::text("abc");
	fileio::WriteOptions w;
	w.dataOnly = true;
	std::string p = path("dataonly.msg");
	Result res = fileio::write(p, m, w);
	ASSERT_TRUE(res.ok());
	EXPECT_EQ("3 bytes written to file " + p, res.text);
	EXPECT_EQ(L::text("abc"), load(p));
}

TEST_F(MessageFile, WriteToMissingDirectoryFails)
{
	Message m;
	Result res = fileio::write("/nonexistent-dir/x.msg", m, fileio::WriteOptions());
	EXPECT_FALSE(res.ok());
	EXPECT_EQ("Unable to open output file - path not found", res.text);
}

TEST_F(MessageFile, LeadingRfh2WithoutMqmdWritesBackIdentically)
{
	auto rfh = L::rfh2(Order::Little, 546, 819, "MQSTR");
	auto golden = L::join({ rfh.data, L::text("data") });
	std::string in = save("leadwrite.msg", golden);
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(in, options(), f, r).ok());

	std::string outPath = path("leadwrite-out.msg");
	Result res = fileio::write(outPath, r.message, fileio::WriteOptions());
	ASSERT_TRUE(res.ok());
	EXPECT_NE(std::string::npos, res.text.find("rfh2 36")) << res.text;
	EXPECT_EQ(golden, load(outPath));
}

TEST_F(MessageFile, Version1MqmdTailIsRebuiltFromTheTab)
{
	L::Bytes md = stringMqmd();
	md.putLong(4, 1);
	std::vector<unsigned char> v1(md.data.begin(), md.data.begin() + O::Length1);
	auto bytes = L::join({ v1, L::text(std::string(60, 'x')) });
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(save("v1tail.msg", bytes), options(), f, r).ok());
	ASSERT_TRUE(r.hadMqmd);
	EXPECT_EQ(1, r.message.mqmd.MsgSeqNumber);
	EXPECT_EQ(0, r.message.mqmd.Offset);
	EXPECT_EQ(0, r.message.mqmd.MsgFlags);
	EXPECT_EQ(-1, r.message.mqmd.OriginalLength);
}

TEST_F(MessageFile, DirectoryFailsLikeAnOpenFailureAndLeavesTheTab)
{
	MqmdFields f;
	f.ccsid = "500";
	f.format = "MQSTR";
	fileio::ReadResult r;
	std::string dir = ::testing::TempDir();
	Result res = fileio::read(dir, options(), f, r);
	EXPECT_FALSE(res.ok());
	EXPECT_EQ("Could not open file " + dir, res.text);
	EXPECT_EQ("500", f.ccsid);
	EXPECT_EQ("MQSTR", f.format);
}

TEST_F(MessageFile, MissingFileLeavesTheTab)
{
	MqmdFields f;
	f.ccsid = "273";
	fileio::ReadResult r;
	EXPECT_FALSE(fileio::read(path("absent"), options(), f, r).ok());
	EXPECT_EQ("273", f.ccsid);
}

TEST_F(MessageFile, IgnoreHeadersTakesPdFormatFromTheTab)
{
	L::Bytes md = stringMqmd();
	md.putLong(O::Encoding, 0x102);
	std::string p = save("pd.msg", L::join({ md.data, L::text("x") }));
	fileio::ReadOptions o = options();
	o.ignoreHeaders = true;
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, o, f, r).ok());
	ASSERT_TRUE(r.hadMqmd);
	EXPECT_EQ(f.pdEncoding, r.pdFormat);
	EXPECT_EQ(NUMERIC_HOST, r.pdFormat);
}

TEST_F(MessageFile, WriteToDirectoryIsAccessDenied)
{
	Message m;
	Result res = fileio::write(::testing::TempDir(), m, fileio::WriteOptions());
	EXPECT_FALSE(res.ok());
	EXPECT_EQ("Unable to open output file - access denied", res.text);
}

TEST_F(MessageFile, WriteToReadOnlyFileIsAccessDenied)
{
	if (geteuid() == 0)
	{
		GTEST_SKIP() << "root ignores file permissions";
	}
	std::string p = save("readonly.msg", L::text("x"));
	ASSERT_EQ(0, chmod(p.c_str(), 0444));
	Message m;
	Result res = fileio::write(p, m, fileio::WriteOptions());
	chmod(p.c_str(), 0644);
	EXPECT_FALSE(res.ok());
	EXPECT_EQ("Unable to open output file - access denied", res.text);
}

TEST_F(MessageFile, DataSourceNamesTheFileAndBodySize)
{
	std::string p = save("src.txt", L::text("a\r\nb"));
	MqmdFields f;
	fileio::ReadResult r;
	fileio::ReadOptions o = options();
	o.removeCrLf = true;
	ASSERT_TRUE(fileio::read(p, o, f, r).ok());
	EXPECT_EQ("Message Data (2) from " + p, r.dataSource) << "the size is after the CR/LF options";
}

TEST_F(MessageFile, DataSourceShortensLongPaths)
{
	std::string dir = ::testing::TempDir() + std::string(90, 'd');
	ASSERT_TRUE(mkdir(dir.c_str(), 0700) == 0 || errno == EEXIST);
	std::string p = dir + "/rfhutil-long.txt";
	std::ofstream(p, std::ios::binary) << "abc";
	ASSERT_GT(p.size(), 75u);
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_EQ("Message Data (3) from ..." + p.substr(p.size() - 75), r.dataSource);
}

TEST_F(MessageFile, MqmdeIsAppliedToTheTabAndNotCounted)
{
	std::string gid = "GROUPIDGROUPIDGROUPID1234";
	gid.resize(24);
	auto mde = L::mde(Order::Little, 546, 1208, "MQSTR", 3, 40, MQMF_MSG_IN_GROUP | MQMF_LAST_MSG_IN_GROUP, 99, gid);
	auto bytes = L::join({ stringMqmd("MQHMDE").data, mde.data, L::text("body") });
	std::string p = save("mde.msg", bytes);
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(p, options(), f, r);
	ASSERT_TRUE(res.ok());
	EXPECT_EQ("4 bytes read from file " + p, res.text);
	EXPECT_EQ("3", f.seqNo);
	EXPECT_EQ("40", f.offset);
	EXPECT_EQ("99", f.originalLength);
	EXPECT_TRUE(f.groupYes);
	EXPECT_TRUE(f.groupLast);
	EXPECT_FALSE(f.segmentYes);
	EXPECT_EQ(0, memcmp(f.groupId, gid.data(), 24));
}

TEST_F(MessageFile, HostMqmdGivesHostFormats)
{
	L::Bytes md = L::defaultMqmd2(Order::Big);
	md.putLong(O::CodedCharSetId, 500, Order::Big);
	md.putLong(O::Encoding, 785, Order::Big);
	md.putChars(O::Format, 8, "MQSTR");
	std::string p = save("host.msg", L::join({ md.data, L::text("X") }));
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_EQ(NUMERIC_HOST, r.fileIntFormat);
	EXPECT_EQ(NUMERIC_HOST, r.numericFormat);
	EXPECT_EQ(NUMERIC_HOST, r.pdFormat);
	EXPECT_EQ(FLOAT_390, r.floatFormat);
}

TEST_F(MessageFile, TabCcsidBeatsAnsiCcsid)
{
	std::string p = save("tabccsid.txt", L::text("hello"));
	MqmdFields f;
	f.ccsid = "819";
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_EQ(819, r.fileCcsid);
}

TEST_F(MessageFile, AnsiFallbackComesFromTheSystemCodePage)
{
	std::string p = save("fallback.txt", L::text("hello"));
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, fileio::ReadOptions(), f, r).ok());
	EXPECT_EQ(rfh_get_acp(), r.fileCcsid);
}

TEST_F(MessageFile, LittleEndianByteOrderMarkSelectsPcEncoding)
{
	std::string p = save("bomle.txt", { 0xFF, 0xFE, 'A', 0x00 });
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, options(), f, r).ok());
	EXPECT_EQ(NUMERIC_PC, r.fileIntFormat);
}

TEST_F(MessageFile, RemoveCrLfWinsOverUnixLineEnds)
{
	std::string p = save("both.txt", L::text("a\nb\r\nc"));
	fileio::ReadOptions o = options();
	o.removeCrLf = true;
	o.unixLineEnds = true;
	MqmdFields f;
	fileio::ReadResult r;
	ASSERT_TRUE(fileio::read(p, o, f, r).ok());
	EXPECT_EQ(L::text("abc"), r.message.body);
}

TEST_F(MessageFile, EmptyFileReadsZeroBytes)
{
	std::string p = save("empty.txt", {});
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(p, options(), f, r);
	ASSERT_TRUE(res.ok());
	EXPECT_EQ("0 bytes read from file " + p, res.text);
	EXPECT_TRUE(r.message.body.empty());
}

TEST_F(MessageFile, FileNameWithAnsiICircumflexByteIsInvalid)
{
	std::string name = std::string("x\xEE") + "y";
	MqmdFields f;
	fileio::ReadResult r;
	Result res = fileio::read(name, options(), f, r);
	EXPECT_FALSE(res.ok());
	EXPECT_EQ("Invalid file name (" + name + ")", res.text);
}
