# Core tests: the existing unit tests (shared with the Windows UnitTests project),
# the platform tests, and the MQ integration tests.

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    find_package(GTest QUIET)
endif()

if(NOT GTest_FOUND)
    if(RFHUTIL_FETCH_GTEST)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
            URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    else()
        message(FATAL_ERROR "GoogleTest not found. Install it (Fedora: sudo dnf install gtest-devel) "
                            "or configure with -DRFHUTIL_FETCH_GTEST=ON to download v1.17.0.")
    endif()
endif()

include(GoogleTest)

add_executable(rfhutil_unit_tests
    UnitTests/ComsubsTests.cpp
    UnitTests/EncodingTests.cpp
    UnitTests/FileFormatTests.cpp
    UnitTests/HeaderUtilTests.cpp
    UnitTests/JsonParseTests.cpp
    UnitTests/MQErrorTests.cpp
    UnitTests/NamesTests.cpp
    UnitTests/ReconnectTests.cpp
    UnitTests/XMLParseTests.cpp
    UnitTests/XmlsubsTests.cpp
    core/PlatformTests.cpp
    core/SharedSourcesTests.cpp
)
target_include_directories(rfhutil_unit_tests PRIVATE UnitTests)
target_link_libraries(rfhutil_unit_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_unit_tests)

add_executable(rfhutil_core_tests core/ResultTests.cpp core/MqsubsTests.cpp core/MqmdFieldsTests.cpp core/HeaderChainTests.cpp core/MessageFileTests.cpp
    core/GuardMallocTests.cpp core/DataFormatterTests.cpp
    core/FormatTests.cpp)
if(NOT MSVC)
    # ResultTests.cpp includes <cmqstrc.h> on purpose; its tables trigger these warnings.
    set_source_files_properties(core/ResultTests.cpp PROPERTIES COMPILE_OPTIONS "-Wno-write-strings;-Wno-unused-function")
endif()
target_link_libraries(rfhutil_core_tests PRIVATE rfhcore GTest::gtest_main ${CMAKE_DL_LIBS})
gtest_discover_tests(rfhutil_core_tests)

add_executable(rfhutil_loader_tests core/MqLoaderTests.cpp)
target_link_libraries(rfhutil_loader_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_loader_tests PROPERTIES LABELS mq)

add_executable(rfhutil_mq_tests core/MqSessionTests.cpp)
target_link_libraries(rfhutil_mq_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_mq_tests PROPERTIES LABELS mq RUN_SERIAL TRUE)

add_executable(rfhutil_mq_fake_tests core/MqSessionFakeTests.cpp)
target_link_libraries(rfhutil_mq_fake_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_mq_fake_tests)

# Memory checking. The guard-page allocator (tools/guardmalloc.cpp) makes a heap
# overrun fault at once rather than corrupt memory silently. The memcheck tests
# (ctest -L memcheck) run the suites that parse message data under it.
add_library(rfh_guardmalloc SHARED tools/guardmalloc.cpp)

# rfh_add_memcheck_test(name target filter [VAR=value ...]) runs the tests that
# match filter under the guard allocator, with any extra environment given.
function(rfh_add_memcheck_test name target filter)
    add_test(NAME ${name} COMMAND ${target} --gtest_filter=${filter})
    set(env "LD_PRELOAD=$<TARGET_FILE:rfh_guardmalloc>")
    list(APPEND env ${ARGN})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "${env}"
        LABELS memcheck
        TIMEOUT 300)
    add_dependencies(${target} rfh_guardmalloc)
endfunction()

rfh_add_memcheck_test(memcheck.GuardMalloc rfhutil_core_tests "GuardMalloc.*")

rfh_add_memcheck_test(memcheck.DataFormatter rfhutil_core_tests "DataFormatter.*")

rfh_add_memcheck_test(memcheck.Parsers rfhutil_unit_tests "JsonParse*:Xmlsubs*:XMLParse*")
rfh_add_memcheck_test(memcheck.Format rfhutil_core_tests "DataFormatter.*:Format_*")
