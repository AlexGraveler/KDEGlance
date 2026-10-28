# SPDX-License-Identifier: GPL-3.0-or-later
configure_file(org.kde.kdeglance.service.in ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kdeglance.service @ONLY)
configure_file(kdeglance.service.in ${CMAKE_CURRENT_BINARY_DIR}/kdeglance.service @ONLY)

install(FILES org.kde.kdeglance.desktop org.kde.kdeglance.settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.kdeglance.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kdeglance.service DESTINATION ${KDEGLANCE_SYSTEMD_USER_UNIT_DIR})
install(FILES icons/org.kde.kdeglance.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES org.kde.kdeglance.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
