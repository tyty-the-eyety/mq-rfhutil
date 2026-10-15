# librfhcore: the portable RFHUtil core (no MFC, no Qt).

add_library(rfhcore STATIC
    platform/rfh_platform.cpp
    platform/rfh_codepage.cpp
    platform/rfh_trace.cpp
)

target_include_directories(rfhcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

if(NOT WIN32)
    set_source_files_properties(
        platform/rfh_platform.cpp platform/rfh_codepage.cpp platform/rfh_trace.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()
