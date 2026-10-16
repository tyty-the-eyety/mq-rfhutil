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

add_executable(rfhutil_core_tests core/ResultTests.cpp core/MqsubsTests.cpp core/MqmdFieldsTests.cpp core/HeaderChainTests.cpp)
if(NOT MSVC)
    # ResultTests.cpp includes <cmqstrc.h> on purpose; its tables trigger these warnings.
    set_source_files_properties(core/ResultTests.cpp PROPERTIES COMPILE_OPTIONS "-Wno-write-strings;-Wno-unused-function")
endif()
target_link_libraries(rfhutil_core_tests PRIVATE rfhcore GTest::gtest_main)
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
