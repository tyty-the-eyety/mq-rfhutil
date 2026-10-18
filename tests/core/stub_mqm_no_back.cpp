// stub_mqm_no_back.cpp - a "library" that exports every MQI entry point the loader needs except MQBACK, so
// that MqLoaderTests can check that a missing MQBACK is reported by name. The functions are never called.

#define RFH_STUB_EXPORT extern "C" __attribute__((visibility("default")))

RFH_STUB_EXPORT void MQCONNX() {}
RFH_STUB_EXPORT void MQDISC() {}
RFH_STUB_EXPORT void MQOPEN() {}
RFH_STUB_EXPORT void MQCLOSE() {}
RFH_STUB_EXPORT void MQPUT() {}
RFH_STUB_EXPORT void MQGET() {}
RFH_STUB_EXPORT void MQINQ() {}
RFH_STUB_EXPORT void MQCMIT() {}
