// rfh_connect_string.h - the MQSERVER-style connection string of the queue manager box.
//
// Windows accepts, in the same box that takes a queue manager name, "CHANNEL/TCP/host(port)" and
// "QM[CHANNEL/TCP/host(port)]" (DataArea::connect2QM, DataArea.cpp:10953-11087, client build only).
// parseServerString ports those forms, with these deliberate differences. Every one of them is a place
// where Windows ignores or hides an error, or reads or writes outside a buffer; here the string is
// rejected with a message and `options` is left exactly as it was.
//
// Windows (line numbers are DataArea.cpp)                          | here
// ------------------------------------------------------------------+---------------------------------
// text of 1023 bytes or more is silently replaced by "" (10960)     | no total limit; each part is checked
// "[CH/TCP/h(1)]" writes mqserver[-1] (10972)                       | accepted, queue manager name empty
// "QM[CH/TCP/h(1)]" connects with the name "" (11005); QM is lost   | qmName = "QM" (MQ then checks it)
// "QM[text]" without a slash ignores the bracket text (11000)       | rejected
// a missing "]" or text after "]" is dropped (10978)                | rejected
// a channel over 20 bytes is silently left blank (11050) or, in the | rejected
//   bracket form, tested against the wrong string (strlen(mqserver)
//   is the QM name) and copied past ChannelName (11053)
// a connection name over 264 bytes is silently left blank (11057)   | rejected
// transport compared exactly; anything else, including "tcp",       | "TCP" in any case; LOCAL, LU62,
//   "", or a misspelling, becomes TCP (11020-11047)                 |   NETBIOS, SPX, DECNET, UDP are named as
//                                                                   |   unsupported; others and "" rejected
// a missing connection name leaves it blank (11057)                 | rejected
// the connection name is passed unchecked                           | each comma-separated entry must be
//                                                                   |   host or host(port), port 1-65535
// whitespace and control characters are passed on                   | outer whitespace trimmed, any other
//                                                                   |   whitespace or control byte rejected
// text is a C string: a NUL ends it                                 | a NUL is rejected
//
// The channel, queue manager name and connection name are stored byte for byte as written (case, leading
// zeros in a port, a bracketed IPv6 host); only the whitespace around the whole string is removed.
// A name with no "/" and no "[" is not a connection string: the function returns true and does not touch
// `options`, so the caller keeps using the name as it does today (and owns trimming it).
#pragma once

#include <string>

#include "rfh_mq_session.h"

namespace rfh
{
	namespace mq
	{
		// On success (a connection string) sets options.mode to Client and options.qmName, channel and
		// connName; every other field of `options` is left alone. Returns false, with a message in `error`
		// and `options` unchanged, when `text` looks like a connection string (it contains "/" or "[") and
		// is malformed, or contains a NUL. `error` is empty whenever the result is true.
		bool parseServerString(const std::string & text, ConnectOptions & options, std::string & error);
	}
}
