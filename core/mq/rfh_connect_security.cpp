// rfh_connect_security.cpp - see rfh_connect_security.h

#include "rfh_connect_security.h"

#include <string.h>

#include <string>

namespace rfh
{
	namespace detail
	{
		namespace
		{
			const size_t kUserIdMax = 255;				// ConnUser.cpp:60
			const size_t kPasswordMax = 1024;			// ConnUser.cpp:62
			const size_t kKeyRepositoryMax = 255;		// MQCHAR256, null-terminated; the dialog stops at 254 (ConnUser.cpp:69)
			const size_t kPeerNameMax = 1024;			// ConnUser.cpp:78; the MQ limit for SSLPeerNameLength
			const MQLONG kResetCountMax = 999999999;	// MQ's maximum KeyResetCount

			Result tooLong(const char * what, size_t limit, size_t actual, const char * unit)
			{
				return Result::failure(std::string("The ") + what + " is longer than " + std::to_string(limit) + " " + unit +
				                       " (it has " + std::to_string(actual) + ")");
			}

			bool hasNul(const std::string & text)
			{
				return text.find('\0') != std::string::npos;
			}

			// Checks a text for a fixed MQ field: no NUL, at most `limit` bytes.
			Result checkField(const char * what, const std::string & text, size_t limit)
			{
				if (hasNul(text))
				{
					return Result::failure(std::string("The ") + what + " contains a NUL character");
				}
				if (text.size() > limit)
				{
					return tooLong(what, limit, text.size(), "characters");
				}
				return Result::success();
			}

			// Copies into a fixed MQ field and pads with NULs, as the Windows structures are initialised.
			void copyNulPadded(MQCHAR * field, size_t fieldLength, const std::string & text)
			{
				memset(field, 0, fieldLength);
				memcpy(field, text.data(), text.size());
			}

			void warn(std::vector<std::string> & warnings, const std::string & text)
			{
				warnings.push_back(text);
			}
		}

		void secureZero(void * data, size_t size)
		{
			volatile unsigned char * p = static_cast<volatile unsigned char *>(data);
			for (size_t i = 0; i < size; i++)
			{
				p[i] = 0;
			}
		}

		void wipePasswordScratch(std::vector<char> & passwordScratch)
		{
			secureZero(passwordScratch.data(), passwordScratch.size());
			passwordScratch.clear();	// the length of the last password is not observable; the storage stays
		}

		Result applyConnectSecurity(const ConnectOptions & options, MQCNO & cno, MQCD * cd, MQSCO & sco, MQCSP & csp,
		                            std::vector<char> & passwordScratch, std::vector<std::string> & warnings)
		{
			const bool client = (options.mode == Mode::Client);

			// ---- credentials (10752-10789; the same in bindings mode) ----
			const bool haveUser = !options.userId.empty();
			const bool havePassword = !options.password.empty();
			if (haveUser && havePassword)
			{
				if (options.userId.size() > kUserIdMax)
				{
					return tooLong("user id", kUserIdMax, options.userId.size(), "bytes");
				}
				if (options.password.size() > kPasswordMax)
				{
					return tooLong("password", kPasswordMax, options.password.size(), "bytes");	// lengths only, never the value
				}
			}

			// ---- client-only settings ----
			bool ssl = false;
			if (!client)
			{
				if (options.sslEnabled || !options.securityExit.empty() || !options.securityData.empty() || !options.localAddress.empty())
				{
					warn(warnings, "TLS, security exit and local address settings apply to client connections only and were ignored for local bindings");
				}
			}
			else
			{
				ssl = options.sslEnabled;
				Result r = Result::success();

				if (!ssl)
				{
					// Windows ignores these silently while the box is unticked (10873); a stale box is easy to miss
					std::string set;
					if (!options.cipherSpec.empty())
					{
						set += "cipher spec";
					}
					if (!options.keyRepository.empty())
					{
						set += set.empty() ? "key repository" : ", key repository";
					}
					if (!options.sslPeerName.empty())
					{
						set += set.empty() ? "peer name" : ", peer name";
					}
					if (!set.empty())
					{
						warn(warnings, "TLS is not enabled, so the TLS " + set + " given was ignored and the connection is not encrypted");
					}
				}

				if (ssl)
				{
					if (cd != nullptr)
					{
						if (options.cipherSpec.empty())
						{
							return Result::failure("TLS is enabled but no TLS cipher spec is set; give a cipher spec or turn TLS off");
						}
						r = checkField("TLS cipher spec", options.cipherSpec, sizeof(cd->SSLCipherSpec));
						if (!r.ok())
						{
							return r;
						}
						if (hasNul(options.sslPeerName) || (options.sslPeerName.size() > kPeerNameMax))
						{
							return hasNul(options.sslPeerName) ? Result::failure("The TLS peer name contains a NUL character")
							                                    : tooLong("TLS peer name", kPeerNameMax, options.sslPeerName.size(), "bytes");
						}
					}
					else
					{
						// The channel definition comes from a CCDT or MQSERVER: a cipher spec given here would be ignored
						// and the user would believe the link is encrypted.
						if (!options.cipherSpec.empty())
						{
							return Result::failure("A TLS cipher spec can only be given with a channel; the CCDT or MQSERVER setting supplies its own");
						}
						if (!options.sslPeerName.empty())
						{
							warn(warnings, "The TLS peer name was ignored: without a channel the connection takes it from the CCDT or MQSERVER");
						}
						if (options.sslClientValidation)
						{
							warn(warnings, "The TLS client validation setting was ignored: without a channel the connection takes it from the CCDT or MQSERVER");
						}
					}

					r = checkField("key repository path", options.keyRepository, kKeyRepositoryMax);
					if (!r.ok())
					{
						return r;
					}
					r = checkField("TLS certificate label", options.certificateLabel, sizeof(sco.CertificateLabel));
					if (!r.ok())
					{
						return r;
					}
					if ((options.sslResetCount < 0) || (options.sslResetCount > kResetCountMax))
					{
						return Result::failure("The TLS reset count must be from 0 to 999999999 (it is " + std::to_string(options.sslResetCount) + ")");
					}
				}

				if (cd != nullptr)
				{
					r = checkField("security exit", options.securityExit, sizeof(cd->SecurityExit));
					if (!r.ok())
					{
						return r;
					}
					r = checkField("security data", options.securityData, sizeof(cd->SecurityUserData));
					if (!r.ok())
					{
						return r;
					}
					r = checkField("local address", options.localAddress, sizeof(cd->LocalAddress));
					if (!r.ok())
					{
						return r;
					}
				}
				else
				{
					if (!options.securityExit.empty())
					{
						warn(warnings, "The security exit was ignored: it belongs to a channel definition and this connection has no channel");
					}
					if (!options.localAddress.empty())
					{
						warn(warnings, "The local address was ignored: it belongs to a channel definition and this connection has no channel");
					}
				}
			}

			// ---- everything is valid; build ----
			if (haveUser && havePassword)
			{
				// MQ is handed a buffer of the session's own, which connect() zeroes after MQCONNX
				passwordScratch.assign(options.password.begin(), options.password.end());
				csp.AuthenticationType = MQCSP_AUTH_USER_ID_AND_PWD;
				csp.CSPUserIdPtr = const_cast<char *>(options.userId.data());
				csp.CSPUserIdLength = (MQLONG)options.userId.size();
				csp.CSPPasswordPtr = passwordScratch.data();
				csp.CSPPasswordLength = (MQLONG)passwordScratch.size();
				cno.SecurityParmsPtr = &csp;
				if (cno.Version < MQCNO_VERSION_5)
				{
					cno.Version = MQCNO_VERSION_5;
				}
			}
			else if (haveUser)
			{
				warn(warnings, "The user id was not sent because no password was given; the connection runs as the process user");
			}
			else if (havePassword)
			{
				warn(warnings, "The password was not sent because no user id was given");
			}

			if (client && (cd != nullptr))
			{
				if (!options.securityExit.empty())
				{
					copyNulPadded(cd->SecurityExit, sizeof(cd->SecurityExit), options.securityExit);
					copyNulPadded(cd->SecurityUserData, sizeof(cd->SecurityUserData), options.securityData);
				}
				else if (!options.securityData.empty())
				{
					warn(warnings, "The security data was ignored because no security exit is named");
				}
				copyNulPadded(cd->LocalAddress, sizeof(cd->LocalAddress), options.localAddress);
			}

			// The session builds the channel definition at version 6. TLS fields, the local address and a
			// keep-alive other than the default lie past MQCD_LENGTH_6; Windows raises it to exactly version 7
			// for TLS (10932-10935). Nothing the core sets lies past MQCD_LENGTH_7.
			if (client && (cd != nullptr) &&
			    (ssl || !options.localAddress.empty() || (options.keepAlive != MQKAI_AUTO)) &&
			    (cd->Version < MQCD_VERSION_7))
			{
				cd->Version = MQCD_VERSION_7;
				cd->StrucLength = MQCD_LENGTH_7;
			}

			if (ssl)
			{
				if (cd != nullptr)
				{
					copyNulPadded(cd->SSLCipherSpec, sizeof(cd->SSLCipherSpec), options.cipherSpec);
					cd->SSLClientAuth = options.sslClientValidation ? MQSCA_REQUIRED : MQSCA_OPTIONAL;
					if (!options.sslPeerName.empty())
					{
						cd->SSLPeerNamePtr = const_cast<char *>(options.sslPeerName.data());
						cd->SSLPeerNameLength = (MQLONG)options.sslPeerName.size();
					}
				}

				sco.Version = MQSCO_VERSION_1;
				if (!options.keyRepository.empty())
				{
					copyNulPadded(sco.KeyRepository, sizeof(sco.KeyRepository), options.keyRepository);
				}
				sco.KeyResetCount = options.sslResetCount;
				if (options.fipsRequired)
				{
					sco.Version = MQSCO_VERSION_2;
					sco.FipsRequired = MQSSL_FIPS_YES;
				}
				if (!options.certificateLabel.empty())
				{
					sco.Version = MQSCO_VERSION_5;
					copyNulPadded(sco.CertificateLabel, sizeof(sco.CertificateLabel), options.certificateLabel);
				}
				cno.SSLConfigPtr = &sco;
			}

			return Result::success();
		}

		std::string describeConnect(const ConnectOptions & options)
		{
			std::string line = "MqSession::connect mode=";
			line += (options.mode == Mode::Client) ? "client" : "bindings";
			line += " qm=" + options.qmName;
			line += " channel=" + options.channel;
			line += " connName=" + options.connName;
			line += options.ccdtUrl.empty() ? " ccdt=not set" : " ccdt=set";	// a URL can carry user:password@
			line += " user=" + options.userId;
			line += options.password.empty() ? " password=not set" : " password=set";
			line += " ssl=" + std::string(options.sslEnabled ? "1" : "0");
			line += " cipher=" + options.cipherSpec;
			line += " keyRepository=" + options.keyRepository;
			line += " peer=" + options.sslPeerName;
			line += " validate=" + std::string(options.sslClientValidation ? "1" : "0");
			line += " fips=" + std::string(options.fipsRequired ? "1" : "0");
			line += " resetCount=" + std::to_string(options.sslResetCount);
			line += " certLabel=" + options.certificateLabel;
			line += " securityExit=" + options.securityExit;
			line += options.securityData.empty() ? " securityData=not set" : " securityData=set";
			line += " localAddress=" + options.localAddress;
			return line;
		}
	}
}
