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

#include "rfh_platform.h"


//
// xmlsubs.h: XML subroutines.header file
//
//////////////////////////////////////////////////////////////////////

int processDTD(const char *dtd, const int remaining);
int processComment(const char *xml, const int remaining);
const char * processEntity(const char *dtdend, const char *eod);
int removeUnneededXML(unsigned char *dataIn, unsigned int len, unsigned char *tempData);
int checkIfXml(const unsigned char *data, const int len, char **errmsg);
int removeVarName(char *xmlVarName, char *VarName);
int removeCrLf(unsigned char *data, unsigned int len);
void removeEscSeq(char *valStr);
char * findEndBracket(char *tempptr);
void findXmlValue(const char *ptr, const char *srch, char *result, const int maxlen);
int findDelim(const unsigned char *datain, 
			  const int maxchar, 
			  const unsigned char delim,
			  const unsigned char escape);
int findDelimW(const RFH_UCS2 *datain, 
			   const int maxchar, 
			   const RFH_UCS2 delim,
			   const RFH_UCS2 escape);
void replaceChars(const char * value, char *valStr);
int insertEscChars(char * output, const char * input, int length);
char * extractAttribute(char *VarName, char *VarValue, char *Ptr);
void processAttributes(char *xmlVarName, char *varName, char *tempVarAttr, char *xmlAttrStr);
int setEscChar(unsigned char *bufferin, unsigned char *bufferout);
char * processTag(char *ptr, const char *tagName, char *dataArea, int maxLen, int *found);
char * buildV1String(char *ptr, const char *name, LPCTSTR value, int curLen);
