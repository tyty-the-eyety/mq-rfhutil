/*
Copyright (c) IBM Corporation 2000, 2018
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Contributors:
Jim MacNair - Initial Contribution
*/

// rfhconst.h - display, character, numeric, persistence and report constants,
// moved from rfhutil.h so that portable sources can use them without MFC.

#pragma once

// Display format types
#define DATA_CHARACTER	0
#define	DATA_HEX		1
#define	DATA_BOTH		2
#define	DATA_XML		3
#define	DATA_PARSED		4
#define	DATA_COBOL		5
#define	DATA_JSON		6
#define DATA_FIX		7

// Character display types
#define CHAR_ASCII		0
#define CHAR_EBCDIC		1
#define CHAR_CHINESE	2
#define CHAR_KOREAN		3
#define CHAR_TRAD_CHIN	4
#define CHAR_JAPANESE	5
#define CHAR_THAI		6
#define CHAR_RUSSIAN	7
#define CHAR_UNICODE	8

#define IGNORE_CRLF		0
#define HONOR_CRLF		1

#define NUMERIC_PC		0
#define NUMERIC_HOST	1

#define FLOAT_PC		0
#define FLOAT_UNIX		1
#define FLOAT_390		2
#define FLOAT_TNS		3

#define BIND_AS_Q		0
#define BIND_ON_OPEN	1
#define BIND_NOT_FIXED	2
#define BIND_GROUP		3

#define MQPERSIST_NO	0
#define MQPERSIST_YES	1
#define MQPERSIST_AS_Q	2

#define REPORT_NONE		0
#define REPORT_YES		1
#define REPORT_DATA		2
#define REPORT_FULL		3

#define GROUP_NO		0
#define GROUP_YES		1
#define GROUP_LAST		2

#define SEGMENT_NO		0
#define SEGMENT_YES		1
#define SEGMENT_LAST	2

// code page defaults
#define RFH_NAMEVALUE_CCSID_DEFAULT 1208
#define DEF_EBCDIC_CCSID			500
#define DEF_ASCII_CCSID				437

// encoding types
#define DEF_PC_ENCODING				546
#define DEF_HOST_ENCODING			785
#define DEF_UNIX_ENCODING			273
