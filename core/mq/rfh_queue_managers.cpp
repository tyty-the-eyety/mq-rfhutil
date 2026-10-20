// rfh_queue_managers.cpp - see rfh_queue_managers.h

#include "rfh_queue_managers.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace rfh
{
	namespace mq
	{
		std::string defaultMqDataPath()
		{
			const char * fromEnv = getenv("MQ_DATA_PATH");

			if ((fromEnv != NULL) && (fromEnv[0] != 0))
			{
				return std::string(fromEnv);
			}
#ifdef _WIN32
			return std::string();
#else
			// Try to read the data path from the MQ installation descriptor.
			// Standard Linux location: /etc/opt/mqm/mqinst.ini (or beside the install
			// given by MQ_INSTALLATION_PATH).  A DataPath= key wins over /var/mqm.
			const char * instPath = getenv("MQ_INSTALLATION_PATH");
			std::string iniCandidates[2];
			int numCandidates = 0;
			if ((instPath != NULL) && (instPath[0] != 0))
			{
				std::string base = instPath;
				while ((base.size() > 1) && (base[base.size() - 1] == '/'))
					base.resize(base.size() - 1);
				iniCandidates[numCandidates++] = base + "/../etc/opt/mqm/mqinst.ini";
			}
			iniCandidates[numCandidates++] = "/etc/opt/mqm/mqinst.ini";

			for (int ci = 0; ci < numCandidates; ci++)
			{
				FILE * f = fopen(iniCandidates[ci].c_str(), "r");
				if (f == NULL) continue;
				char line[512];
				while (fgets(line, (int)sizeof(line), f) != NULL)
				{
					const char * p = line;
					while (*p == ' ' || *p == '\t') p++;
					if (strncmp(p, "DataPath", 8) != 0) continue;
					p += 8;
					while (*p == ' ' || *p == '\t') p++;
					if (*p != '=') continue;
					p++;
					while (*p == ' ' || *p == '\t') p++;
					std::string val(p);
					while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' '))
						val.resize(val.size() - 1);
					if (!val.empty())
					{
						fclose(f);
						return val;
					}
				}
				fclose(f);
			}
			return std::string("/var/mqm");
#endif
		}

#ifdef _WIN32
		Result listLocalQueueManagers(const std::string & mqDataPath, LocalQueueManagers & out)
		{
			out = LocalQueueManagers();
			out.iniPath = mqDataPath;
			return Result::failure("Listing local queue managers from mqs.ini is not supported on Windows");
		}
#else
		namespace
		{
			enum Section { SectionNone, SectionQueueManager, SectionDefault, SectionOther };

			// A QueueManager: stanza being read.
			struct Pending
			{
				int line = 0;
				std::string name;
				std::optional<std::string> prefix;
				std::optional<std::string> directory;
				std::optional<std::string> dataPath;
			};

			bool isBlank(char c)
			{
				return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\f') || (c == '\v');
			}

			std::string trim(const std::string & s)
			{
				size_t b = 0;
				size_t e = s.size();

				while ((b < e) && isBlank(s[b]))
				{
					b++;
				}
				while ((e > b) && isBlank(s[e - 1]))
				{
					e--;
				}
				return s.substr(b, e - b);
			}

			std::string lower(const std::string & s)
			{
				std::string r = s;

				for (size_t i = 0; i < r.size(); i++)
				{
					if ((r[i] >= 'A') && (r[i] <= 'Z'))
					{
						r[i] = (char)(r[i] - 'A' + 'a');
					}
				}
				return r;
			}

			// Strict UTF-8: no overlong forms, no surrogates, nothing above U+10FFFF.
			bool isValidUtf8(const std::string & s)
			{
				size_t i = 0;
				const size_t n = s.size();

				while (i < n)
				{
					unsigned char c = (unsigned char)s[i];
					size_t extra;
					unsigned long cp;

					if (c < 0x80)
					{
						i++;
						continue;
					}
					else if ((c >= 0xC2) && (c <= 0xDF))
					{
						extra = 1;
						cp = c & 0x1F;
					}
					else if ((c >= 0xE0) && (c <= 0xEF))
					{
						extra = 2;
						cp = c & 0x0F;
					}
					else if ((c >= 0xF0) && (c <= 0xF4))
					{
						extra = 3;
						cp = c & 0x07;
					}
					else
					{
						return false;
					}

					if (i + extra >= n)
					{
						return false;
					}
					for (size_t k = 1; k <= extra; k++)
					{
						unsigned char cc = (unsigned char)s[i + k];

						if ((cc & 0xC0) != 0x80)
						{
							return false;
						}
						cp = (cp << 6) | (cc & 0x3F);
					}
					if (((extra == 2) && (cp < 0x800)) || ((extra == 3) && (cp < 0x10000)) ||
						((cp >= 0xD800) && (cp <= 0xDFFF)) || (cp > 0x10FFFF))
					{
						return false;
					}
					i += extra + 1;
				}
				return true;
			}

			// Collects warnings up to kMaxWarnings and counts the rest.
			class Warnings
			{
			public:
				explicit Warnings(Result & r) : result(r) {}

				void add(int line, const std::string & text)
				{
					add("line " + std::to_string(line) + ": " + text);
				}

				void add(const std::string & text)
				{
					if (result.warnings.size() < kMaxWarnings)
					{
						result.warnings.push_back(text);
					}
					else
					{
						omitted++;
					}
				}

				void finish()
				{
					if (omitted > 0)
					{
						result.warnings.push_back(std::to_string(omitted) + " further warnings omitted");
					}
				}

			private:
				Result & result;
				size_t omitted = 0;
			};

			// Reads at most kMaxIniBytes + 1 bytes. found is false for a file that does not exist.
			Result readIni(const std::string & path, std::string & data, bool & found, bool & tooBig)
			{
				found = false;
				tooBig = false;
				data.clear();

				FILE * f = fopen(path.c_str(), "rb");

				if (f == NULL)
				{
					int err = errno;

					if ((err == ENOENT) || (err == ENOTDIR))
					{
						return Result::success();
					}
					return Result::failure("Cannot open " + path + ": " + strerror(err));
				}

				found = true;

				char buffer[8192];

				while (data.size() <= kMaxIniBytes)
				{
					size_t want = kMaxIniBytes + 1 - data.size();

					if (want > sizeof(buffer))
					{
						want = sizeof(buffer);
					}

					size_t got = fread(buffer, 1, want, f);

					data.append(buffer, got);
					if (got < want)
					{
						if (ferror(f))
						{
							int err = errno;

							fclose(f);
							data.clear();
							return Result::failure("Cannot read " + path + ": " + strerror(err));
						}
						break;
					}
				}
				fclose(f);

				if (data.size() > kMaxIniBytes)
				{
					tooBig = true;
					data.resize(kMaxIniBytes);
				}
				return Result::success();
			}

			void checkUtf8(const char * field, const std::string & value, std::vector<std::string> & notes)
			{
				if (!isValidUtf8(value))
				{
					notes.push_back(std::string(field) + " is not valid UTF-8; the bytes are kept as read");
				}
			}

			// Turns a finished stanza into a list entry, or a warning when it cannot be listed.
			void finish(const Pending & p, LocalQueueManagers & out, std::unordered_map<std::string, size_t> & index, Warnings & warn)
			{
				if (p.name.empty())
				{
					std::string what = "QueueManager stanza has no Name and is skipped";

					if (p.directory.has_value())
					{
						what += " (Directory=" + *p.directory + ")";
					}
					else if (p.prefix.has_value())
					{
						what += " (Prefix=" + *p.prefix + ")";
					}
					else if (p.dataPath.has_value())
					{
						what += " (DataPath=" + *p.dataPath + ")";
					}
					warn.add(p.line, what);
					return;
				}

				std::unordered_map<std::string, size_t>::const_iterator seen = index.find(p.name);

				if (seen != index.end())
				{
					warn.add(p.line, "queue manager " + p.name + " is defined again (first at line " +
									 std::to_string(out.managers[seen->second].line) + "); the repeat is ignored");
					return;
				}

				LocalQueueManager q;

				q.name = p.name;
				q.prefix = p.prefix;
				q.directory = p.directory;
				q.dataPath = p.dataPath;
				q.line = p.line;

				if (q.name.size() > kMaxQueueManagerNameLength)
				{
					q.notes.push_back("Name has " + std::to_string(q.name.size()) + " characters; MQ accepts at most " +
									  std::to_string(kMaxQueueManagerNameLength));
					warn.add(p.line, "queue manager name " + q.name.substr(0, kMaxQueueManagerNameLength) + "... is longer than " +
									 std::to_string(kMaxQueueManagerNameLength) + " characters");
				}
				checkUtf8("Name", q.name, q.notes);
				if (q.prefix.has_value())
				{
					checkUtf8("Prefix", *q.prefix, q.notes);
				}
				if (q.directory.has_value())
				{
					checkUtf8("Directory", *q.directory, q.notes);
				}
				if (q.dataPath.has_value())
				{
					checkUtf8("DataPath", *q.dataPath, q.notes);
				}

				// DataPath alone says where the data is; otherwise Prefix and Directory are both needed
				if (!q.dataPath.has_value() && (!q.prefix.has_value() || !q.directory.has_value()))
				{
					if (!q.prefix.has_value())
					{
						q.notes.push_back("Prefix absent");
					}
					if (!q.directory.has_value())
					{
						q.notes.push_back("Directory absent");
					}
					q.notes.push_back("DataPath absent");
				}

				index[q.name] = out.managers.size();
				out.managers.push_back(q);
			}

			// Stores a value in an optional field; the first one wins.
			void setOnce(std::optional<std::string> & field, const std::string & value, int line, const char * key, Warnings & warn)
			{
				if (field.has_value())
				{
					warn.add(line, std::string("repeated ") + key + "= is ignored; the first value is kept");
				}
				else
				{
					field = value;
				}
			}

			void parse(const std::string & data, LocalQueueManagers & out, Warnings & warn)
			{
				Section section = SectionNone;
				Pending pending;
				std::unordered_map<std::string, size_t> index;		// name -> position in out.managers
				int lineNo = 0;
				size_t pos = 0;

				if (data.compare(0, 3, "\xEF\xBB\xBF") == 0)
				{
					pos = 3;
				}

				while (pos < data.size())
				{
					size_t end = data.find('\n', pos);

					if (end == std::string::npos)
					{
						end = data.size();
					}

					std::string raw = data.substr(pos, end - pos);

					pos = end + 1;
					lineNo++;

					if (raw.find('\0') != std::string::npos)
					{
						warn.add(lineNo, "line contains a NUL byte and is skipped");
						continue;
					}

					std::string text = trim(raw);

					if (text.empty() || (text[0] == '#'))
					{
						continue;
					}

					size_t eq = text.find('=');

					if ((eq == std::string::npos) && (text[text.size() - 1] == ':'))
					{
						// a stanza header; the one before it is complete
						if (section == SectionQueueManager)
						{
							finish(pending, out, index, warn);
						}

						std::string word = lower(trim(text.substr(0, text.size() - 1)));

						if (word == "queuemanager")
						{
							section = SectionQueueManager;
							pending = Pending();
							pending.line = lineNo;
						}
						else if (word == "defaultqueuemanager")
						{
							section = SectionDefault;
						}
						else
						{
							section = SectionOther;
						}
						continue;
					}

					if (eq == std::string::npos)
					{
						warn.add(lineNo, "not a stanza header or a key=value setting; ignored");
						continue;
					}

					if (section == SectionNone)
					{
						warn.add(lineNo, "setting outside any stanza; ignored");
						continue;
					}

					std::string key = lower(trim(text.substr(0, eq)));
					std::string value = trim(text.substr(eq + 1));

					if (section == SectionQueueManager)
					{
						if (key == "name")
						{
							if (!pending.name.empty())
							{
								warn.add(lineNo, "repeated Name= is ignored; the first value is kept");
							}
							else
							{
								pending.name = value;
							}
						}
						else if (key == "prefix")
						{
							setOnce(pending.prefix, value, lineNo, "Prefix", warn);
						}
						else if (key == "directory")
						{
							setOnce(pending.directory, value, lineNo, "Directory", warn);
						}
						else if (key == "datapath")
						{
							setOnce(pending.dataPath, value, lineNo, "DataPath", warn);
						}
					}
					else if ((section == SectionDefault) && (key == "name"))
					{
						if (value.empty())
						{
							warn.add(lineNo, "DefaultQueueManager has an empty Name=; ignored");
						}
						else
						{
							setOnce(out.defaultName, value, lineNo, "Name", warn);
						}
					}
				}

				if (section == SectionQueueManager)
				{
					finish(pending, out, index, warn);
				}
			}
		}

		Result listLocalQueueManagers(const std::string & mqDataPath, LocalQueueManagers & out)
		{
			out = LocalQueueManagers();

			std::string base = mqDataPath.empty() ? defaultMqDataPath() : mqDataPath;

			while ((base.size() > 1) && (base[base.size() - 1] == '/'))
			{
				base.resize(base.size() - 1);
			}
			out.iniPath = (base == "/") ? std::string("/mqs.ini") : base + "/mqs.ini";

			Result result;
			Warnings warn(result);
			std::string data;
			bool tooBig = false;
			Result read = readIni(out.iniPath, data, out.fileFound, tooBig);

			if (!read.ok())
			{
				return read;
			}
			if (!out.fileFound)
			{
				warn.add("No " + out.iniPath + " (no local IBM MQ server installation, or MQ_DATA_PATH points elsewhere)");
				warn.finish();
				return result;
			}

			if (tooBig)
			{
				out.truncated = true;

				// a line cut by the limit could hold half a name: drop it
				size_t lastNewline = data.rfind('\n');

				data.resize((lastNewline == std::string::npos) ? 0 : lastNewline + 1);
				warn.add(out.iniPath + " is larger than " + std::to_string(kMaxIniBytes) +
						 " bytes: truncated, only the complete lines within the first " + std::to_string(kMaxIniBytes) + " bytes were read");
			}

			parse(data, out, warn);

			if (out.defaultName.has_value())
			{
				bool matched = false;

				for (size_t i = 0; i < out.managers.size(); i++)
				{
					if (out.managers[i].name == *out.defaultName)
					{
						out.managers[i].isDefault = true;
						matched = true;
					}
				}
				if (!matched)
				{
					warn.add("DefaultQueueManager names " + *out.defaultName + ", which no listed QueueManager stanza defines");
				}
			}

			warn.finish();
			return result;
		}
#endif
	}
}
