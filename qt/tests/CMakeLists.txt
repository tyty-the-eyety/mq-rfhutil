# Qt Test executables. Each runs headless (QT_QPA_PLATFORM=offscreen) under ctest.

function(rfhqt_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE rfhqt Qt6::Test)
    # tests/core/fake_mq.h, the in-memory MQI shared with the core tests
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests/core)
    add_test(NAME qt.${name} COMMAND ${name})
    set_tests_properties(qt.${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        LABELS qt)
endfunction()

rfhqt_add_test(rfhqt_mainwindow_tests MainWindowTests.cpp)
rfhqt_add_test(rfhqt_sessioncontroller_tests SessionControllerTests.cpp)
rfhqt_add_test(rfhqt_appsettings_tests AppSettingsTests.cpp)
rfhqt_add_test(rfhqt_statuslog_tests StatusLogTests.cpp)
rfhqt_add_test(rfhqt_filetrace_tests FileTraceTests.cpp)
rfhqt_add_test(rfhqt_appdocument_tests AppDocumentTests.cpp)
rfhqt_add_test(rfhqt_operations_tests OperationsTests.cpp)
target_compile_definitions(rfhqt_operations_tests PRIVATE RFH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/core/golden")
rfhqt_add_test(rfhqt_maintab_tests MainTabTests.cpp)
