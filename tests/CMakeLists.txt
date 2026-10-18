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
    core/FormatTests.cpp core/FormatFuzzTests.cpp core/StatusTextTests.cpp core/IdDisplayTests.cpp
    core/QueueManagersTests.cpp core/ConnectStringTests.cpp)
if(NOT MSVC)
    # ResultTests.cpp includes <cmqstrc.h> on purpose; its tables trigger these warnings.
    set_source_files_properties(core/ResultTests.cpp PROPERTIES COMPILE_OPTIONS "-Wno-write-strings;-Wno-unused-function")
endif()
target_link_libraries(rfhutil_core_tests PRIVATE rfhcore GTest::gtest_main ${CMAKE_DL_LIBS})
gtest_discover_tests(rfhutil_core_tests)

# The golden message files (tests/core/golden/) are written by tools/golden/make_goldens.cpp,
# which uses only tests/core/mq_layout.h and deliberately does not link rfhcore: the files
# must not depend on the code they test. The tests read them through the core.
add_executable(rfh_make_goldens ../tools/golden/make_goldens.cpp)
target_include_directories(rfh_make_goldens PRIVATE core)
if(NOT MSVC)
    target_compile_options(rfh_make_goldens PRIVATE -Wall -Wextra)
endif()

add_executable(rfhutil_golden_tests core/GoldenFileTests.cpp)
target_link_libraries(rfhutil_golden_tests PRIVATE rfhcore GTest::gtest_main)
target_compile_definitions(rfhutil_golden_tests PRIVATE
    RFH_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/core/golden"
    RFH_MAKE_GOLDENS="$<TARGET_FILE:rfh_make_goldens>"
    RFH_COMPARE_SCRIPT="${PROJECT_SOURCE_DIR}/tools/golden/compare-windows-output.sh")
add_dependencies(rfhutil_golden_tests rfh_make_goldens)
gtest_discover_tests(rfhutil_golden_tests)

add_executable(rfhutil_loader_tests core/MqLoaderTests.cpp)
target_link_libraries(rfhutil_loader_tests PRIVATE rfhcore GTest::gtest_main)
# A library without MQCMIT, for the loader's missing-entry-point test (Linux only: it is loaded with dlopen).
if(UNIX)
    add_library(rfh_stub_mqm_no_cmit SHARED core/stub_mqm_no_cmit.cpp)
    set_target_properties(rfh_stub_mqm_no_cmit PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    add_dependencies(rfhutil_loader_tests rfh_stub_mqm_no_cmit)
    target_compile_definitions(rfhutil_loader_tests PRIVATE RFH_STUB_NO_CMIT="$<TARGET_FILE:rfh_stub_mqm_no_cmit>")
endif()
gtest_discover_tests(rfhutil_loader_tests PROPERTIES LABELS mq)

add_executable(rfhutil_mq_tests core/MqSessionTests.cpp)
target_link_libraries(rfhutil_mq_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_mq_tests PROPERTIES LABELS mq RUN_SERIAL TRUE)

add_executable(rfhutil_mq_fake_tests core/MqSessionFakeTests.cpp core/ListQueuesTests.cpp core/GetOptionsTests.cpp core/ConnectSecurityTests.cpp)
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

# The PCF reply parser and the queue listing read replies of any length and shape; run them with exact-size buffers under the guard allocator.
rfh_add_memcheck_test(memcheck.ListQueues rfhutil_mq_fake_tests "ListQueues.*:PcfQueueReply.*")

# The connection string parser reads caller text of any shape; run it on exact-size strings under the guard allocator.
rfh_add_memcheck_test(memcheck.ConnectString rfhutil_core_tests "ConnectString.*")
rfh_add_memcheck_test(memcheck.ConnectSecurity rfhutil_mq_fake_tests "ConnectSecurityTest.*:ConnectOptionsWipe.*")

rfh_add_memcheck_test(memcheck.Parsers rfhutil_unit_tests "JsonParse*:Xmlsubs*:XMLParse*")
rfh_add_memcheck_test(memcheck.Format rfhutil_core_tests "Format_*")
rfh_add_memcheck_test(memcheck.FormatFuzz rfhutil_core_tests "FormatFuzz.*" FORMAT_FUZZ_ITERATIONS=3000)

# The core must not mention a UI toolkit, an MFC call or a process exit (tests/core/CoreHasNoUiTests.cmake).
# core.NoUiSelfTest proves the script reports each pattern and has no false positives on ordinary words;
# core.NoUiBadFixture must fail, so it shows that a scan of an offending directory is rejected by ctest.
set(rfh_no_ui_script ${CMAKE_CURRENT_SOURCE_DIR}/core/CoreHasNoUiTests.cmake)
set(rfh_no_ui_work ${CMAKE_CURRENT_BINARY_DIR}/no_ui_fixture)
add_test(NAME core.NoUi
    COMMAND ${CMAKE_COMMAND} -DSCAN_DIR=${PROJECT_SOURCE_DIR}/core -P ${rfh_no_ui_script})
add_test(NAME core.NoUiSelfTest
    COMMAND ${CMAKE_COMMAND} -DSELFTEST=1 -DWORK_DIR=${rfh_no_ui_work} -P ${rfh_no_ui_script})
add_test(NAME core.NoUiBadFixture
    COMMAND ${CMAKE_COMMAND} -DSCAN_DIR=${rfh_no_ui_work}/bad -P ${rfh_no_ui_script})
set_tests_properties(core.NoUiSelfTest PROPERTIES FIXTURES_SETUP rfh_no_ui_fixture)
set_tests_properties(core.NoUiBadFixture PROPERTIES FIXTURES_REQUIRED rfh_no_ui_fixture WILL_FAIL TRUE)
