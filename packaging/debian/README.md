# Debian / Ubuntu

This recipe builds a binary `.deb` from the working tree. It has been built on
**Ubuntu 26.04 LTS (resolute)**, amd64, with Plasma 6.6, Qt 6.10.2 and KDE
Frameworks 6.24, and the full CTest suite passes there; manual desktop
acceptance on Ubuntu has not been recorded yet. Debian and Ubuntu releases with
Qt >= 6.8 and KF6 >= 6.0 are expected to work, but each release needs its own
build: Qt refuses plugins compiled against a newer Qt minor, so do not copy a
package between releases.

The resulting package is unsigned and intended for local use. A distributable
upload should go through a clean build (`sbuild`, `pbuilder`, or a PPA).

## Build

```bash
sudo apt install debhelper devscripts cmake ninja-build pkgconf \
    extra-cmake-modules qt6-base-dev qt6-multimedia-dev qt6-svg-dev \
    qt6-5compat-dev libkf6coreaddons-dev libkf6kio-dev libkf6service-dev \
    libkf6windowsystem-dev libpoppler-qt6-dev libraw-dev \
    dbus-daemon desktop-file-utils ffmpeg imagemagick libreoffice-writer \
    python3 qpdf
packaging/debian/build-deb.sh
```

`apt build-dep .` does not work from this directory because `debian/` lives one
level down; the command above is the authoritative list (`debian/control` also
records it).

The helper stages tracked files into `packaging/debian/build/source`, copies the
recipe in as `source/debian`, and runs `dpkg-buildpackage -b -us -uc`. Build
products stay in the ignored `packaging/debian/build/` directory. Uncommitted
source edits are included; commits are not required.

The full CTest suite runs during `dpkg-buildpackage`. It needs LibreOffice
Writer, FFmpeg, ImageMagick, Python and qpdf for the generated fixtures; Node.js
additionally runs the KWin script simulation when it is on `PATH`. Tests are
headless (`QT_QPA_PLATFORM=offscreen`).

## Install

```bash
sudo apt install ./packaging/debian/build/kdeglance_2.0.4-1_amd64.deb
```

Close and reopen Dolphin so it loads the new plugins. The viewer itself starts
on demand through the user D-Bus session; no service needs enabling and no
global shortcut is created.

## Remove

```bash
sudo apt remove kdeglance
```

Settings in `~/.config/kdeglance` are preserved.

## What differs from the Arch and Fedora recipes

- The Qt plugin directory is Debian's multiarch path,
  `/usr/lib/x86_64-linux-gnu/qt6/plugins`, not `/usr/lib/qt6/plugins` or
  `/usr/lib64/qt6/plugins`. CMake derives it from the Qt build, so no override
  is needed.
- `/usr/lib/systemd/user/kdeglance.service` is activated by D-Bus only; the unit
  has no `[Install]` section. `dh_installsystemduser --no-start` refreshes the
  unit without enabling it.
- `/usr/share/licenses/` (Arch/Fedora convention) is dropped; the license notice
  lives in `/usr/share/doc/kdeglance/copyright`.
- LibreOffice, fluidsynth, the SoundFont and the optional image plugins are
  weak dependencies (`Recommends`), matching the RPM. Installing the package
  does not pull them in.

## Files

| Path | Purpose |
|---|---|
| `control` | source/binary metadata, build and runtime dependencies |
| `rules` | debhelper overrides (CMake options, headless tests, systemd user unit) |
| `changelog` | Debian version, distribution and uploader |
| `copyright` | DEP-5 license statement |
| `source/format` | `3.0 (quilt)` |
| `build-deb.sh` | stages the tree and runs `dpkg-buildpackage` |
| `kdeglance.prerm` | stops a resident viewer on upgrade or removal |

A `kdeglance-dbgsym_*.ddeb` debug package is produced next to the `.deb`. It is
not needed for local use.

## Before proposing changes upstream

`Maintainer` and the changelog trailer are deliberately a neutral project
identity, not the author of this recipe: submitting the packaging does not make
the contributor responsible for it. Before a real upload, replace both with the
person or team who will own the package (Debian KDE team, Ubuntu MOTU, PPA
owner). Debian archive uploads require a real human in the changelog trailer;
the sponsoring uploader normally add their own entry anyway.

`debian/copyright` credits the packaging files to the project identity used by
the source headers. Adjust it if a contributor holds the copyright personally.

Ubuntu-only dependencies (`libqt6core6t64`, `kf6` package names, `t64` suffixes)
differ from Debian, so a single control file may need adjustment per
distribution. The recommended workflow for a real upload is a Debian KDE team
repository or an Ubuntu PPA rather than a package attached to a GitHub release.
