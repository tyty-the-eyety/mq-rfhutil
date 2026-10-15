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

	NullTrace nullTrace;
	RfhTrace * currentTrace = &nullTrace;
}

RfhTrace * rfhGetTrace()
{
	return currentTrace;
}

void rfhSetTrace(RfhTrace * trace)
{
	currentTrace = (trace != NULL) ? trace : &nullTrace;
}
