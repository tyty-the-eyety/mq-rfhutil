// rfh_mq_session.cpp - see rfh_mq_session.h (stub until Task 5)

#include "rfh_mq_session.h"

namespace rfh
{
	namespace { Result notImplemented() { return Result::failure("MqSession is not implemented yet"); } }

	MqSession::~MqSession() {}
	Result MqSession::connect(const ConnectOptions &) { return notImplemented(); }
	Result MqSession::disconnect() { return notImplemented(); }
	Result MqSession::put(const std::string &, Message &, const PutOptions &) { return notImplemented(); }
	Result MqSession::get(const std::string &, Message &, const GetOptions &) { return notImplemented(); }
	Result MqSession::depth(const std::string &, MQLONG &) { return notImplemented(); }
	Result MqSession::purge(const std::string &, MQLONG &) { return notImplemented(); }
	Result MqSession::closeQueue(CloseOption) { return notImplemented(); }
}
