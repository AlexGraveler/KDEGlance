# SPDX-License-Identifier: GPL-3.0-or-later

# --- core: settings, geometry, file types, navigation (no widgets) -----------
add_library(kdeglance_core STATIC
    core/constants.h
    core/settings.cpp
    core/geometrymath.cpp
    core/filetype.cpp
    core/navigationlist.cpp
    core/l10n.cpp
)
target_include_directories(kdeglance_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_BINARY_DIR}
)
target_link_libraries(kdeglance_core PUBLIC Qt6::Core Qt6::Gui)
target_compile_definitions(kdeglance_core PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_URL_CAST_FROM_STRING QT_NO_KEYWORDS)

# --- viewer -----------------------------------------------------------------------
add_library(kdeglance_viewer STATIC
    viewer/defaultfileopener.cpp
    viewer/remoteresolver.cpp
    viewer/style.cpp
    viewer/formatregistry.cpp
    viewer/formatprobe.cpp
    viewer/kwinintegration.cpp
    viewer/previewwindow.cpp
    viewer/settingsdialog.cpp
    viewer/viewerapp.cpp
    viewer/views/contentview.cpp
    viewer/views/imagedecoder.cpp
    viewer/views/rawdecoder.cpp
    viewer/views/imageloader.cpp
    viewer/views/animationplayer.cpp
    viewer/views/imageview.cpp
    viewer/views/mediacontrols.cpp
    viewer/views/mediaview.cpp
    viewer/views/officeview.cpp
    viewer/views/pdfview.cpp
    viewer/views/textview.cpp
    viewer/views/infoview.cpp
)
target_include_directories(kdeglance_viewer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/viewer)
target_link_libraries(kdeglance_viewer PUBLIC kdeglance_core Qt6::Widgets Qt6::DBus Qt6::Multimedia Qt6::Core5Compat KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::Service PkgConfig::POPPLER_QT6)
if(HAVE_QTSVG)
    target_link_libraries(kdeglance_viewer PUBLIC Qt6::Svg)
endif()
if(HAVE_LIBRAW)
    target_link_libraries(kdeglance_viewer PUBLIC PkgConfig::LIBRAW)
endif()
if(HAVE_KWINDOWSYSTEM)
    target_link_libraries(kdeglance_viewer PUBLIC KF6::WindowSystem)
endif()

qt_add_resources(decoder_check_resource viewer/decodercheck.qrc)
add_executable(kdeglance viewer/main.cpp viewer/mediabackend.cpp ${decoder_check_resource})
target_link_libraries(kdeglance PRIVATE kdeglance_viewer)
install(TARGETS kdeglance RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES kwin/placement.js DESTINATION ${CMAKE_INSTALL_DATADIR}/kdeglance/kwin)
# Lets a build-tree binary find the script (bin/../share/kdeglance/kwin).
configure_file(kwin/placement.js ${CMAKE_BINARY_DIR}/share/kdeglance/kwin/placement.js COPYONLY)

# --- Dolphin integration ----------------------------------------------------------
if(BUILD_DOLPHIN_INTEGRATION)
    add_library(kdeglance_dolphinhook MODULE
        dolphin/hook/hookplugin.cpp
        dolphin/hook/bridge.cpp
        dolphin/hook/dolphinaccess.cpp
    )
    target_link_libraries(kdeglance_dolphinhook PRIVATE kdeglance_core Qt6::Widgets Qt6::DBus KF6::KIOCore ${CMAKE_DL_LIBS})
    set_target_properties(kdeglance_dolphinhook PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/kf6/overlayicon")
    install(TARGETS kdeglance_dolphinhook LIBRARY DESTINATION ${KDEGLANCE_QT_PLUGIN_DIR}/kf6/overlayicon)

    add_library(kdeglance_quicklook MODULE dolphin/menu/quicklookaction.cpp)
    target_link_libraries(kdeglance_quicklook PRIVATE kdeglance_core Qt6::Widgets Qt6::DBus KF6::KIOWidgets KF6::CoreAddons)
    set_target_properties(kdeglance_quicklook PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/kf6/kfileitemaction")
    install(TARGETS kdeglance_quicklook LIBRARY DESTINATION ${KDEGLANCE_QT_PLUGIN_DIR}/kf6/kfileitemaction)
endif()
