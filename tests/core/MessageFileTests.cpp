// MessageFileTests.cpp — tests for rfh::fileio (ports of DataArea::ReadFileData and
// DataArea::WriteFile). The golden files are built with mq_layout.h at IBM's
// documented offsets, independent of the code under test.

#include <gtest/gtest.h>

#include <stdio.h>
#include <string.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <cmqc.h>

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

	Message out = r.message;
	out.mqmd = MQMD2{ MQMD2_DEFAULT };
	out.mqmd.Version = MQMD_VERSION_2;
	mqmd_fields::toMqmd(f, out.mqmd);

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
