# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 REQUIRED COMPONENTS Test)

# Every test runs headless (offscreen QPA), in English, with its own config dir.
function(kdeglance_test name)
    cmake_parse_arguments(ARG "DBUS" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(ARG_DBUS AND DBUS_RUN_SESSION)
        add_test(NAME ${name} COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:${name}>)
    else()
        add_test(NAME ${name} COMMAND ${name})
    endif()
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;KDEGLANCE_LANG=en;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-${name}"
        TIMEOUT 300)
endfunction()

kdeglance_test(settingstest SOURCES unit/settingstest.cpp LIBS kdeglance_core)
kdeglance_test(geometrytest SOURCES unit/geometrytest.cpp LIBS kdeglance_core)
kdeglance_test(filetypetest SOURCES unit/filetypetest.cpp LIBS kdeglance_core)
kdeglance_test(imageviewtest SOURCES viewer/imageviewtest.cpp LIBS kdeglance_viewer)
kdeglance_test(mediaviewtest SOURCES viewer/mediaviewtest.cpp LIBS kdeglance_viewer)
kdeglance_test(previewwindowtest SOURCES viewer/previewwindowtest.cpp LIBS kdeglance_viewer)
kdeglance_test(viewerapptest DBUS SOURCES viewer/viewerapptest.cpp LIBS kdeglance_viewer)
kdeglance_test(stresstest SOURCES viewer/stresstest.cpp LIBS kdeglance_viewer)

# Format support with real samples, decoded by the installed decoders.
add_test(NAME formats
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/formats/run-formats.sh $<TARGET_FILE:kdeglance> ${CMAKE_CURRENT_BINARY_DIR}/format-samples)
set_tests_properties(formats PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;KDEGLANCE_LANG=en" TIMEOUT 900)

# Dolphin integration: mock Dolphin + real hook/menu plugins + real viewer,
# viewer started by D-Bus activation on a private bus.
if(BUILD_DOLPHIN_INTEGRATION)
    add_library(mockdolphinprivate SHARED dolphin/mockdolphin.cpp)
    target_link_libraries(mockdolphinprivate PUBLIC Qt6::Widgets KF6::KIOCore)
    add_executable(dolphinintegrationtest dolphin/dolphinintegrationtest.cpp)
    target_link_libraries(dolphinintegrationtest PRIVATE mockdolphinprivate kdeglance_core Qt6::Test Qt6::DBus KF6::KIOWidgets KF6::CoreAddons)
    target_include_directories(dolphinintegrationtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(dolphinintegrationtest PRIVATE
        KDEGLANCE_HOOK_PLUGIN="$<TARGET_FILE:kdeglance_dolphinhook>"
        KDEGLANCE_MENU_PLUGIN="$<TARGET_FILE:kdeglance_quicklook>")
    add_dependencies(dolphinintegrationtest kdeglance kdeglance_dolphinhook kdeglance_quicklook)
    add_test(NAME dolphinintegrationtest
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/dolphin/run-integration.sh $<TARGET_FILE:dolphinintegrationtest> $<TARGET_FILE:kdeglance>)
    set_tests_properties(dolphinintegrationtest PROPERTIES TIMEOUT 600)
endif()

# Renders the window for visual review into kdeglance-screenshots/ (not a check).
add_executable(screenshots viewer/screenshots.cpp)
target_link_libraries(screenshots PRIVATE kdeglance_viewer Qt6::Test)

# KWin placement script against a simulated KWin scripting API (needs Node.js).
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME kwinplacementtest COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/kwin/placementtest.js ${PROJECT_SOURCE_DIR}/src/kwin/placement.js)
endif()

# Exercise the production preflight with a hardware path unavailable on many
# machines. The CLI must still deliver a CPU-readable AV1 frame.
add_test(NAME av1fallback COMMAND $<TARGET_FILE:kdeglance> --probe-formats ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/av1)
set_tests_properties(av1fallback PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FFMPEG_DECODING_HW_DEVICE_TYPES=vulkan;KDEGLANCE_LANG=en;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-av1fallback"
    PASS_REGULAR_EXPRESSION "video frame decoded"
    TIMEOUT 20)

# Explicitly opt in; never injected into normal Dolphin installations.
option(BUILD_DESKTOP_TEST_ADAPTER "Build a Qt event adapter for dedicated real desktop test processes" OFF)
if(BUILD_DESKTOP_TEST_ADAPTER)
    add_library(sessionadapter MODULE desktop/sessionadapter.cpp)
    target_link_libraries(sessionadapter PRIVATE Qt6::Widgets Qt6::DBus Qt6::Test)
endif()

kdeglance_test(documenttest SOURCES viewer/documenttest.cpp LIBS kdeglance_viewer)
add_test(NAME documentformats COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/documents/run-formats.sh $<TARGET_FILE:kdeglance> ${CMAKE_CURRENT_BINARY_DIR}/document-samples)
set_tests_properties(documentformats PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;KDEGLANCE_LANG=en" TIMEOUT 600)
find_package(Qt6 REQUIRED COMPONENTS Network)
kdeglance_test(remoteresolvertest DBUS SOURCES viewer/remoteresolvertest.cpp LIBS kdeglance_viewer Qt6::Network)

kdeglance_test(defaultopentest DBUS SOURCES viewer/defaultopentest.cpp LIBS kdeglance_viewer)
target_compile_definitions(defaultopentest PRIVATE TEST_DOCUMENT_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/documents/sample.fodt")
add_executable(residentopenprobe viewer/residentopenprobe.cpp)
target_link_libraries(residentopenprobe PRIVATE kdeglance_viewer Qt6::Test)
add_dependencies(defaultopentest residentopenprobe)
target_compile_definitions(defaultopentest PRIVATE TEST_RESIDENT_PROBE="$<TARGET_FILE:residentopenprobe>")
