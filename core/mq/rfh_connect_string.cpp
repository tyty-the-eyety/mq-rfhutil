#include "rfh_connect_string.h"

#include <cstddef>
#include <string>

namespace rfh
{
	namespace mq
	{
		namespace
		{
			bool isSpace(char c)
			{
				return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n') || (c == '\v') || (c == '\f');
			}

			bool isDigit(char c)
			{
				return (c >= '0') && (c <= '9');
			}

			// the text, at most 40 bytes of it, for a message; bytes that would not print are shown as '?'
			std::string shown(const std::string & text)
			{
				std::string out;
				for (std::size_t i = 0; (i < text.size()) && (i < 40); i++)
				{
					const unsigned char c = static_cast<unsigned char>(text[i]);
					out += ((c > 0x20) && (c < 0x7F)) ? static_cast<char>(c) : '?';
				}
				if (text.size() > 40)
				{
					out += "...";
				}
				return out;
			}

			bool fail(std::string & error, const std::string & message)
			{
				error = message;
				return false;
			}

			// Space, control and non-ASCII bytes cannot be in any part. Windows passes them on to MQ.
			bool checkBytes(const std::string & part, const std::string & what, std::string & error)
			{
				for (std::size_t i = 0; i < part.size(); i++)
				{
					const unsigned char c = static_cast<unsigned char>(part[i]);
					if (c >= 0x80)
					{
						return fail(error, "The " + what + " contains a non-ASCII byte (at offset " + std::to_string(i) + "): " + shown(part));
					}
					if ((c <= 0x20) || (c == 0x7F))
					{
						return fail(error, "The " + what + " contains a space or control character (at offset " + std::to_string(i) +
						                       "); only the ends of the whole string may have whitespace: " + shown(part));
					}
				}
				return true;
			}

			// A channel or queue manager name: the bytes above, and none of the characters that give the string its shape.
			bool checkName(const std::string & name, std::size_t limit, bool mayBeEmpty, const std::string & what, std::string & error)
			{
				if (name.empty() && !mayBeEmpty)
				{
					return fail(error, "The " + what + " is missing");
				}
				if (name.size() > limit)
				{
					return fail(error, "The " + what + " is " + std::to_string(name.size()) + " characters; MQ allows at most " + std::to_string(limit));
				}
				if (!checkBytes(name, what, error))
				{
					return false;
				}
				if (name.find_first_of("[]()/,") != std::string::npos)
				{
					return fail(error, "The " + what + " contains one of [ ] ( ) / ,: " + shown(name));
				}
				return true;
			}

			// host or host(port), at most one pair of parentheses, and nothing after ")"
			bool checkEntry(const std::string & entry, std::string & error)
			{
				if (entry.empty())
				{
					return fail(error, "The connection name has an empty entry (a leading, trailing or doubled comma)");
				}

				std::string host = entry;
				std::string port;
				bool hasPort = false;
				const std::size_t open = entry.find('(');
				if (open != std::string::npos)
				{
					if (entry.back() != ')')
					{
						return fail(error, "The connection name entry must end with ')' after the port: " + shown(entry));
					}
					host = entry.substr(0, open);
					port = entry.substr(open + 1, entry.size() - open - 2);
					hasPort = true;
					if ((port.find_first_of("()") != std::string::npos) || (host.find(')') != std::string::npos))
					{
						return fail(error, "The connection name entry has more than one pair of parentheses: " + shown(entry));
					}
				}
				else if (entry.find(')') != std::string::npos)
				{
					return fail(error, "The connection name entry has ')' without '(': " + shown(entry));
				}

				if (host.empty())
				{
					return fail(error, "The connection name entry has no host: " + shown(entry));
				}
				if (host.find('/') != std::string::npos)
				{
					return fail(error, "The connection name has a '/' in the host (a CHANNEL/TCP/ prefix has too many parts?): " + shown(entry));
				}
				if ((host[0] == '[') || (host.find_first_of("[]") != std::string::npos))
				{
					// only a complete bracketed literal, passed on unchanged
					const bool literal = (host[0] == '[') && (host.size() > 2) && (host.back() == ']') &&
					                     (host.find_first_of("[]", 1) == host.size() - 1);
					if (!literal)
					{
						return fail(error, "The connection name host has unmatched or misplaced square brackets: " + shown(entry));
					}
				}

				if (hasPort)
				{
					if (port.empty())
					{
						return fail(error, "The connection name entry has an empty port: " + shown(entry));
					}
					unsigned long value = 0;
					for (char c : port)
					{
						if (!isDigit(c))
						{
							return fail(error, "The port is not a decimal number from 1 to 65535: " + shown(entry));
						}
						if (value <= 65535)
						{
							value = value * 10 + static_cast<unsigned long>(c - '0');
						}
					}
					if ((value < 1) || (value > 65535))
					{
						return fail(error, "The port is not a decimal number from 1 to 65535: " + shown(entry));
					}
				}
				return true;
			}

			bool checkConnectionName(const std::string & conn, std::string & error)
			{
				if (conn.empty())
				{
					return fail(error, "The connection name is missing; write CHANNEL/TCP/host(port)");
				}
				if (conn.size() > MQ_CONN_NAME_LENGTH)
				{
					return fail(error, "The connection name is " + std::to_string(conn.size()) + " characters; MQ allows at most " +
					                       std::to_string(static_cast<int>(MQ_CONN_NAME_LENGTH)));
				}
				if (!checkBytes(conn, "connection name", error))
				{
					return false;
				}

				std::size_t start = 0;
				while (true)
				{
					const std::size_t comma = conn.find(',', start);
					const std::string entry = conn.substr(start, (comma == std::string::npos) ? std::string::npos : comma - start);
					if (!checkEntry(entry, error))
					{
						return false;
					}
					if (comma == std::string::npos)
					{
						return true;
					}
					start = comma + 1;
				}
			}

			bool isTcp(const std::string & transport)
			{
				return (transport.size() == 3) && ((transport[0] | 0x20) == 't') && ((transport[1] | 0x20) == 'c') && ((transport[2] | 0x20) == 'p');
			}

			std::string upperAscii(std::string text)
			{
				for (char & c : text)
				{
					if ((c >= 'a') && (c <= 'z'))
					{
						c = static_cast<char>(c - 'a' + 'A');
					}
				}
				return text;
			}
		}

		bool parseServerString(const std::string & text, ConnectOptions & options, std::string & error)
		{
			error.clear();

			if (text.find('\0') != std::string::npos)
			{
				return fail(error, "The connection string contains a NUL character at offset " + std::to_string(text.find('\0')));
			}

			std::size_t first = 0;
			std::size_t last = text.size();
			while ((first < last) && isSpace(text[first]))
			{
				first++;
			}
			while ((last > first) && isSpace(text[last - 1]))
			{
				last--;
			}
			const std::string s = text.substr(first, last - first);

			const std::size_t slash = s.find('/');
			const std::size_t open = s.find('[');
			if ((slash == std::string::npos) && (open == std::string::npos))
			{
				return true;		// a queue manager name
			}

			std::string qm;
			std::string body = s;
			if ((open != std::string::npos) && ((slash == std::string::npos) || (open < slash)))
			{
				if (s.back() != ']')
				{
					return fail(error, "The text after '[' must end with ']' and have nothing after it: QM[CHANNEL/TCP/host(port)]");
				}
				qm = s.substr(0, open);
				body = s.substr(open + 1, s.size() - open - 2);
				if (body.find('/') == std::string::npos)
				{
					return fail(error, "The text between '[' and ']' must be CHANNEL/TCP/host(port): " + shown(body));
				}
				if (!checkName(qm, MQ_Q_MGR_NAME_LENGTH, true, "queue manager name", error))
				{
					return false;
				}
			}

			const std::size_t firstSlash = body.find('/');
			const std::string channel = body.substr(0, firstSlash);
			if (!checkName(channel, MQ_CHANNEL_NAME_LENGTH, false, "channel name", error))
			{
				return false;
			}

			const std::string rest = body.substr(firstSlash + 1);
			const std::size_t secondSlash = rest.find('/');
			const std::string transport = rest.substr(0, secondSlash);
			if (transport.empty())
			{
				return fail(error, "The transport is missing; only TCP is supported: CHANNEL/TCP/host(port)");
			}
			if (!isTcp(transport))
			{
				const std::string name = upperAscii(transport);
				const bool known = (name == "LOCAL") || (name == "LU62") || (name == "NETBIOS") || (name == "SPX") || (name == "DECNET") || (name == "UDP");
				return fail(error, known ? "The transport " + shown(transport) + " is not supported on this platform; use TCP"
				                         : "The transport " + shown(transport) + " is not one MQ knows; use TCP");
			}
			if (secondSlash == std::string::npos)
			{
				return fail(error, "The connection name is missing; write CHANNEL/TCP/host(port)");
			}

			const std::string conn = rest.substr(secondSlash + 1);
			if (!checkConnectionName(conn, error))
			{
				return false;
			}

			options.mode = Mode::Client;
			options.qmName = qm;
			options.channel = channel;
			options.connName = conn;
			return true;
		}
	}
}
