// rfh_result.cpp - see rfh_result.h

#include "rfh_result.h"

#include <stdio.h>

#include "mqerror.h"

// cmqstrc.h defines its lookup functions in the header, so include it in exactly
// one translation unit.
#include <cmqstrc.h>

namespace rfh
{
	Result Result::failure(const std::string & text)
	{
		Result r;
		r.cc = MQCC_FAILED;
		r.rc = MQRC_NONE;
		r.text = text;
		return r;
	}

	Result Result::fromMq(const char * operation, MQLONG cc, MQLONG rc)
	{
		Result r;
		r.cc = cc;
		r.rc = rc;

		if ((cc != MQCC_OK) || (rc != MQRC_NONE))
		{
			char text[512];
			const char * description = mqReasonString((long)rc);

			if (description != NULL)
			{
				snprintf(text, sizeof(text), "%s cc=%d rc=%d %s (%s)", operation, (int)cc, (int)rc, reasonName(rc), description);
			}
			else
			{
				snprintf(text, sizeof(text), "%s cc=%d rc=%d %s", operation, (int)cc, (int)rc, reasonName(rc));
			}

			r.text = text;
		}

		return r;
	}

	const char * reasonName(MQLONG rc)
	{
		const char * name = MQRC_STR(rc);
		return (name != NULL && name[0] != 0) ? name : "MQRC_UNKNOWN";
	}
}
