# SPDX-License-Identifier: GPL-3.0-or-later
Name:           kdeglance
Version:        2.0.3
Release:        1%{?dist}
Summary:        Quick Look style file previews for Dolphin
License:        GPL-3.0-or-later
URL:            https://github.com/AlexGraveler/KDEGlance
# Generated locally by make-source-tarball.py; no published Fedora release yet.
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  extra-cmake-modules
BuildRequires:  qt6-rpm-macros
BuildRequires:  systemd-rpm-macros
BuildRequires:  pkgconfig(Qt6Core) >= 6.8
BuildRequires:  pkgconfig(Qt6Gui)
BuildRequires:  pkgconfig(Qt6Widgets)
BuildRequires:  pkgconfig(Qt6DBus)
BuildRequires:  pkgconfig(Qt6Multimedia)
BuildRequires:  pkgconfig(Qt6Core5Compat)
BuildRequires:  pkgconfig(Qt6Svg)
BuildRequires:  pkgconfig(Qt6Test)
BuildRequires:  pkgconfig(poppler-qt6)
BuildRequires:  pkgconfig(libraw_r)
BuildRequires:  cmake(KF6KIO)
BuildRequires:  cmake(KF6Service)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6WindowSystem)
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
# Test fixtures only: none of these add a LibreOffice runtime requirement.
BuildRequires:  dbus-daemon
BuildRequires:  /usr/bin/ffmpeg
BuildRequires:  ImageMagick
BuildRequires:  python3
BuildRequires:  nodejs
BuildRequires:  qpdf
BuildRequires:  libreoffice-writer

# Qt rejects plugins compiled against a newer minor version.
Requires:       qt6-qtbase%{?_isa} >= %{_qt6_version}
Requires:       dolphin%{?_isa}
Requires:       qt6-qtwayland%{?_isa}
Requires:       qt6-qtimageformats%{?_isa}
Requires:       kf6-kimageformats%{?_isa}
Requires:       kio-fuse%{?_isa}
Requires:       hicolor-icon-theme
Recommends:     kio-extras
%{?systemd_requires}

%description
Select a file in Dolphin and press Space to preview images, camera RAW,
audio, video, PDF and text. Office document previews use LibreOffice when
installed separately; it is not required at runtime.
Includes Dolphin Space and context-menu plugins and an on-demand D-Bus service.

%prep
%autosetup

%build
%cmake -G Ninja \
    -DKDEGLANCE_QT_PLUGIN_DIR=%{_qt6_plugindir} \
    -DKDEGLANCE_SYSTEMD_USER_UNIT_DIR=%{_userunitdir} \
    -DBUILD_DOLPHIN_INTEGRATION=ON \
    -DWITH_LIBRAW=ON \
    -DWITH_KWINDOWSYSTEM=ON \
    -DBUILD_TESTING=ON
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/*.desktop
appstream-util validate-relax --nonet %{buildroot}%{_metainfodir}/org.kde.kdeglance.metainfo.xml
QT_QPA_PLATFORM=offscreen %ctest --output-on-failure --timeout 900

%post
%systemd_user_post kdeglance.service

%preun
%systemd_user_preun kdeglance.service

%postun
%systemd_user_postun_with_restart kdeglance.service

%files
%license %{_datadir}/licenses/%{name}/LICENSE
%doc %{_docdir}/%{name}/
%{_bindir}/kdeglance
%{_qt6_plugindir}/kf6/overlayicon/kdeglance_dolphinhook.so
%{_qt6_plugindir}/kf6/kfileitemaction/kdeglance_quicklook.so
%{_userunitdir}/kdeglance.service
%{_datadir}/dbus-1/services/org.kde.kdeglance.service
%{_datadir}/applications/org.kde.kdeglance.desktop
%{_datadir}/applications/org.kde.kdeglance.settings.desktop
%{_datadir}/icons/hicolor/scalable/apps/org.kde.kdeglance.svg
%{_metainfodir}/org.kde.kdeglance.metainfo.xml
%{_datadir}/kdeglance/

%changelog
* Tue Nov 10 2026 KDEGlance contributors - 2.0.3-1
- Unify Fedora 44 and Arch/CachyOS geometry and cursor zoom fixes
- Preserve optional LibreOffice runtime and Fedora plugin paths

* Tue Nov 10 2026 KDEGlance contributors - 2.0.2-3
- Commit cursor zoom and initial media geometry with KWin configure
- Test intermediate resize states and paused-video dimensions

* Fri Nov 06 2026 KDEGlance contributors - 2.0.2-2
- Correct initial aspect fit and cursor anchored photo/video zoom

* Fri Nov 06 2026 KDEGlance contributors - 2.0.2-1
- Initial Fedora package with Dolphin integration and optional LibreOffice runtime
