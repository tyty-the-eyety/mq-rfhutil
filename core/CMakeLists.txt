# librfhcore: the portable RFHUtil core (no MFC, no Qt).

if(CMAKE_CXX_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    message(FATAL_ERROR "the shared RFHUtil sources assume a little-endian host")
endif()

# Shared sources that also build into the Windows MFC application. They stay in
# RFHUtil/ so the Visual Studio projects keep their paths.
set(RFHUTIL_SHARED_SOURCES
    ${PROJECT_SOURCE_DIR}/RFHUtil/comsubs.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/xmlsubs.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/JsonParse.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/XMLParse.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/Names.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/mqerror.cpp
)

add_library(rfhcore STATIC
    platform/rfh_platform.cpp
    platform/rfh_codepage.cpp
    platform/rfh_trace.cpp
    ${RFHUTIL_SHARED_SOURCES}
)

target_include_directories(rfhcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        ${PROJECT_SOURCE_DIR}/RFHUtil
)

if(NOT WIN32)
    # The shared sources include "stdafx.h" and "rfhutil.h", which pull in MFC on
    # Windows. Supply the portable stand-ins: compat/ provides stdafx.h and the
    # prelude blocks rfhutil.h (the technique tests/UnitTests/prelude.h uses).
    # -fsigned-char: the shared parsers were written for MSVC, where plain char is
    # signed (as on x86-64 Linux); aarch64 and ppc64le default to unsigned.
    target_include_directories(rfhcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat)
    set_source_files_properties(${RFHUTIL_SHARED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/compat/core_prelude.h;-fsigned-char")

    # New core code is held to a clean warning level; the shared sources keep
    # their historic warnings, which are only in trace formatting.
    set_source_files_properties(
        platform/rfh_platform.cpp platform/rfh_codepage.cpp platform/rfh_trace.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()
