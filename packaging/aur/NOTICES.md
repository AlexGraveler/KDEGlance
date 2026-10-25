# KDEGlance package licenses

KDEGlance and the package launch/setup helpers are GPL-3.0-or-later.
The full GPL text is installed alongside this file as LICENSE.
The corresponding upstream source archive, PKGBUILD and all helper sources are
included in the AUR build inputs. The native bridge is compiled from native.cpp.

This package uses Arch's python-mutagen dependency (GPL-2.0-or-later). It does
not install the vendored Mutagen copy in the upstream source archive. Qt, PyQt6,
KDE Frameworks and codec libraries are also system dependencies; their installed
packages retain their own upstream license notices. The open-source PyQt6 route
uses GPLv3. No Qt/KDE binaries are bundled inside the upstream source archive.

Icons and the audio placeholder are drawn by the included application code.
No separate icon pack, font, example media or screenshot is installed.
