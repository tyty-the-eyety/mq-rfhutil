// stub_mqm_no_cmit.cpp - a "library" that exports the MQI entry points that existed before Task 14 and
// not MQCMIT, so that MqLoaderTests can check that a missing entry point is reported by name.
// The functions are never called.

#define RFH_STUB_EXPORT extern "C" __attribute__((visibility("default")))

RFH_STUB_EXPORT void MQCONNX() {}
RFH_STUB_EXPORT void MQDISC() {}
RFH_STUB_EXPORT void MQOPEN() {}
RFH_STUB_EXPORT void MQCLOSE() {}
RFH_STUB_EXPORT void MQPUT() {}
RFH_STUB_EXPORT void MQGET() {}
RFH_STUB_EXPORT void MQINQ() {}
