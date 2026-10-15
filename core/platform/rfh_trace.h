// rfh_trace.h - trace sink used by the shared RFHUtil sources.
//
// The shared sources used to reach the trace through (CRfhutilApp *)AfxGetApp().
// They now call rfhGetTrace(). The MFC application object implements RfhTrace and
// installs itself with rfhSetTrace(this); other hosts install their own sink.
// rfhGetTrace() never returns NULL: without a host sink it returns a no-op sink.
#pragma once

#include "rfh_platform.h"

class RfhTrace
{
public:
	virtual ~RfhTrace() {}

	virtual BOOL isTraceEnabled() = 0;
	virtual void logTraceEntry(const char * traceInfo) = 0;
	virtual void dumpTraceData(const char * label, const unsigned char * data, unsigned int length) = 0;

	// Verbose trace also dumps data areas. Read directly by the shared sources.
	BOOL verboseTrace = FALSE;
};

RfhTrace * rfhGetTrace();

// Installs the process-wide trace sink. NULL restores the built-in no-op sink.
// Not synchronised: install the sink before starting threads that trace.
void rfhSetTrace(RfhTrace * trace);
