# Cross-platform build for the portable RFHUtil core and its tests.
# The Windows MFC application keeps building with RFHUtil.sln.
cmake_minimum_required(VERSION 3.21)

project(rfhutil VERSION 10.0.0.240 LANGUAGES CXX)

if(WIN32)
    message(FATAL_ERROR "The CMake build is Linux-only for now; on Windows use RFHUtil.sln")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json in the build directory, for clangd and other tools.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RFHUTIL_BUILD_TESTS "Build the core tests" ON)
option(RFHUTIL_FETCH_GTEST "Download GoogleTest when no installed package is found" OFF)
option(RFHUTIL_BUILD_QT "Build the Qt6 application when Qt6 Widgets is installed" ON)

set(RFHUTIL_DEFAULT_MQ_HOME "/opt/mqm")
set(MQ_HOME "${RFHUTIL_DEFAULT_MQ_HOME}" CACHE PATH "IBM MQ installation directory (headers only; the library is loaded at run time)")

add_subdirectory(core)

if(RFHUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(RFHUTIL_BUILD_QT)
    add_subdirectory(qt)
endif()

# ---- Packaging (CPack) ----------------------------------------------------------------------------
# Build an RPM or DEB from the linux-release preset and then run:
#   cpack -G RPM --config build/linux-release/CPackConfig.cmake
#   cpack -G DEB --config build/linux-release/CPackConfig.cmake
#
# IBM MQ is not listed as a hard dependency: the application works without it
# (queue operations are unavailable) and MQ has its own installer.

if(RFHUTIL_BUILD_QT)
    set(CPACK_PACKAGE_NAME "rfhutil-qt")
    set(CPACK_PACKAGE_VENDOR "RFHUtil Contributors")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "IBM MQ message utility")
    set(CPACK_PACKAGE_DESCRIPTION
"RfhUtil is an IBM MQ message utility for viewing, editing and
transferring messages on IBM MQ queues. It supports MQRFH2 headers,
browse/get/put, file read and write, and TLS client connections.")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_CONTACT "https://github.com/ibm-messaging/mq-rfhutil")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    # Install under /usr so the binary lands in /usr/bin and the desktop file
    # in /usr/share/applications, matching the distro filesystem layout.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    # RPM (Fedora / RHEL / SUSE)
    set(CPACK_RPM_PACKAGE_LICENSE "Apache-2.0")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Communications")
    # Auto-require shared libraries (default); the Qt6 GUI and core packages
    # are picked up automatically from the binary's ELF NEEDED entries.
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    # Conventional file name: rfhutil-qt-10.0.0.240-1.x86_64.rpm
    set(CPACK_RPM_PACKAGE_RELEASE "1")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

    # DEB (Debian / Ubuntu)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6 (>= 6.0) | libqt6widgets6t64 (>= 6.0)")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    # Conventional file name: rfhutil-qt_10.0.0.240_amd64.deb
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    include(CPack)
endif()
