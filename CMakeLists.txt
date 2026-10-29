# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 KDEGlance contributors
cmake_minimum_required(VERSION 3.22)

project(kdeglance VERSION 2.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

include(GNUInstallDirs)
include(FeatureSummary)

option(BUILD_TESTING "Build the automated tests" ON)
option(BUILD_DOLPHIN_INTEGRATION "Build the Dolphin hook and context menu plugins (needs KF6 KIO)" ON)
option(WITH_LIBRAW "Camera RAW support through LibRaw" ON)
option(WITH_KWINDOWSYSTEM "Compositor-drawn window shadow through KF6 WindowSystem" ON)

set(QT_MIN_VERSION 6.8.0)
set(KF_MIN_VERSION 6.0.0)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets DBus Multimedia Core5Compat)
find_package(Qt6 ${QT_MIN_VERSION} OPTIONAL_COMPONENTS Svg Test)

set(HAVE_QTSVG ${Qt6Svg_FOUND})

set(HAVE_LIBRAW 0)
if(WITH_LIBRAW)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBRAW IMPORTED_TARGET libraw_r)
        if(NOT LIBRAW_FOUND)
            pkg_check_modules(LIBRAW IMPORTED_TARGET libraw)
        endif()
    endif()
    if(LIBRAW_FOUND)
        set(HAVE_LIBRAW 1)
    endif()
endif()
add_feature_info(LibRaw HAVE_LIBRAW "camera RAW previews (CR2/CR3/NEF/ARW/RAF/ORF/RW2/DNG/...)")

set(HAVE_KWINDOWSYSTEM 0)
if(WITH_KWINDOWSYSTEM)
    find_package(KF6WindowSystem ${KF_MIN_VERSION} CONFIG)
    if(KF6WindowSystem_FOUND)
        set(HAVE_KWINDOWSYSTEM 1)
    endif()
endif()
add_feature_info(KWindowSystem HAVE_KWINDOWSYSTEM "compositor-drawn shadow around the preview window")

find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_QT6 REQUIRED IMPORTED_TARGET poppler-qt6)

find_package(KF6KIO ${KF_MIN_VERSION} CONFIG REQUIRED)
find_package(KF6Service ${KF_MIN_VERSION} CONFIG REQUIRED)
if(BUILD_DOLPHIN_INTEGRATION)
    find_package(KF6CoreAddons ${KF_MIN_VERSION} CONFIG REQUIRED)
endif()

# Where Qt looks for plugins (kf6/overlayicon, kf6/kfileitemaction live below).
if(NOT KDEGLANCE_QT_PLUGIN_DIR)
    if(QT6_INSTALL_PLUGINS)
        set(KDEGLANCE_QT_PLUGIN_DIR "${QT6_INSTALL_PLUGINS}")
    else()
        set(KDEGLANCE_QT_PLUGIN_DIR "${CMAKE_INSTALL_LIBDIR}/qt6/plugins")
    endif()
endif()
set(KDEGLANCE_SYSTEMD_USER_UNIT_DIR "lib/systemd/user" CACHE STRING "systemd user unit directory (relative to prefix)")

configure_file(config-kdeglance.h.in ${CMAKE_CURRENT_BINARY_DIR}/config-kdeglance.h)

add_subdirectory(src)
add_subdirectory(data)

if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/kdeglance)
install(FILES README.md COPYING LICENSE CHANGELOG.md RELEASE-NOTES.md CONTRIBUTING.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES docs/ARCHITECTURE.md docs/FORMATS.md docs/TESTING.md docs/INSTALL.md
        docs/RELEASE-VALIDATION.md docs/MANUAL-ACCEPTANCE.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(DIRECTORY docs/screenshots/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs/screenshots
        FILES_MATCHING PATTERN "*.png")

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
