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
    src/CicsTab.cpp
    src/ConnTab.cpp
    src/ConnUserDialog.cpp
    src/DataTab.cpp
    src/DlqTab.cpp
    src/FileTrace.cpp
    src/FindDialogs.cpp
    src/ImsTab.cpp
    src/JmsTab.cpp
    src/MainTab.cpp
    src/MainWindow.cpp
    src/MqmdTab.cpp
    src/OtherTab.cpp
    src/PscrTab.cpp
    src/PsTab.cpp
    src/PubSubTab.cpp
    src/RfhTab.cpp
    src/UsrPropTab.cpp
    src/UsrTab.cpp
    src/IdEdit.h
    src/Operations.cpp
    src/LoadQDialog.cpp
    src/SaveQDialog.cpp
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
target_compile_definitions(rfhutil-qt PRIVATE
    RFHUTIL_VERSION="${PROJECT_VERSION}")

install(TARGETS rfhutil-qt RUNTIME DESTINATION bin)
install(FILES rfhutil-qt.desktop DESTINATION share/applications)

if(RFHUTIL_BUILD_TESTS)
    add_subdirectory(tests)
endif()
