// rfh_trace.cpp - see rfh_trace.h

#include "rfh_trace.h"

namespace
{
	class NullTrace : public RfhTrace
	{
	public:
		BOOL isTraceEnabled() override { return FALSE; }
		void logTraceEntry(const char *) override {}
		void dumpTraceData(const char *, const unsigned char *, unsigned int) override {}
	};

	// Function-local so the sink exists during static initialisation and destruction.
	RfhTrace & nullTrace()
	{
		static NullTrace t;
		return t;
	}

	RfhTrace * currentTrace = nullptr;
}

RfhTrace * rfhGetTrace()
{
	return currentTrace ? currentTrace : &nullTrace();
}

void rfhSetTrace(RfhTrace * trace)
{
	currentTrace = trace;
}
