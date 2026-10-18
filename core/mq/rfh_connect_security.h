// rfh_connect_security.h - the security half of MqSession::connect: TLS, security exit, local address and
// credentials, put into the MQI structures. Internal to the session (its tests drive it through connect()).
//
// Windows source: DataArea::connect2QM, DataArea.cpp:10752-10951 and 11066-11086; limits from ConnUser.cpp 59-79.
// Every rule below is pinned by tests/core/ConnectSecurityTests.cpp. Where Windows hides a problem the rule here
// differs, and says so:
//
// Windows (DataArea.cpp lines)                                        | here
// --------------------------------------------------------------------+---------------------------------------
// TLS applies only when "use SSL" is ticked AND a cipher spec is      | with a channel, TLS without a cipher spec is
//   typed (10873); ticked with no cipher spec connects unencrypted    |   rejected (the user asked for encryption)
// cipher spec of 32 characters is cut to 31 (10877)                   | 32 accepted (the field holds 32), 33 rejected
// key repository of 255 or more characters is ignored (10900)         | up to 255 accepted, longer rejected
// security user data is used only if an exit is named (10829-10855)   | ignored with a warning
// user id alone, or a password alone, is dropped (10754, 10772)       | dropped with a warning
// security exit, data, local address longer than the field are cut    | rejected
//   (10833, 10848, 11070)
// user id and password in the MQCD (12 characters each, cut) when the | not ported: a client without MQCSP support
//   environment variable RFHUTIL_PASSWORD_CD is set or the client is  |   (before MQ 6) is not supported; the MQCSP is
//   older than version 6 (10758-10816)                                |   always used
// SSLPeerName set in the MQCD; reset count taken as given (10907)     | peer name up to 1024 bytes; reset count 0 to
//                                                                     |   999999999, others rejected
// TLS and local address exist only in the client build (#ifdef        | one binary: a bindings connection ignores them
//   MQCLIENT); the bindings build ignores them silently               |   with a warning when any is set
//
// The local address is also unreachable from the Windows connection dialog (General::OnSetConnUser, General.cpp
// 2347-2412, never copies m_local_address); here it is a plain option.
//
// Passwords: the password is copied into a scratch buffer for MQCSP and the buffer is zeroed as soon as MQCONNX
// returns. Nothing here puts a password into a message, a warning or a trace line.
#pragma once

#include <string>
#include <vector>

#include "rfh_mq_session.h"

namespace rfh
{
	namespace detail
	{
		// Fills `sco` and `csp` and the security fields of `cd`, and points `cno` at them, for the options given.
		// `cd` is the channel definition cno.ClientConnPtr points at, or null when none is used (bindings, CCDT,
		// MQSERVER). `passwordScratch` receives the password as MQCSP will see it. Fails, without any MQ call and
		// with nothing of the password in the message, for a value MQ would cut or refuse. Warnings go to `warnings`.
		Result applyConnectSecurity(const ConnectOptions & options, MQCNO & cno, MQCD * cd, MQSCO & sco, MQCSP & csp,
		                            std::vector<char> & passwordScratch, std::vector<std::string> & warnings);

		// Overwrites memory in a way the compiler may not drop.
		void secureZero(void * data, size_t size);

		// One line describing the connect for the trace. The password is "set" or "not set", never its value.
		std::string describeConnect(const ConnectOptions & options);
	}
}
