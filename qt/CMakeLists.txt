# The Qt6 application: a static library of widgets and logic (so the tests can use it)
# and the rfhutil-qt executable. Built only when Qt6 Widgets is installed.

find_package(Qt6 COMPONENTS Widgets Test QUIET)
if(NOT Qt6_FOUND)
    message(STATUS "Qt6 Widgets and Test not found; the Qt application is not built "
                   "(Fedora: sudo dnf install qt6-qtbase-devel)")
    return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

add_library(rfhqt STATIC
    src/AppDocument.cpp
    src/AppSettings.cpp
    src/ConnUserDialog.cpp
    src/FileTrace.cpp
    src/MainTab.cpp
    src/MainWindow.cpp
    src/MqmdTab.cpp
    src/IdEdit.h
    src/Operations.cpp
    src/ProcessSetup.cpp
    src/SessionController.cpp
    src/StatusLog.cpp
    src/PopupComboBox.h
)
target_include_directories(rfhqt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rfhqt PUBLIC Qt6::Widgets rfhcore)
target_compile_options(rfhqt PRIVATE -Wall -Wextra)

add_executable(rfhutil-qt src/main.cpp)
target_link_libraries(rfhutil-qt PRIVATE rfhqt)

if(RFHUTIL_BUILD_TESTS)
    add_subdirectory(tests)
endif()
