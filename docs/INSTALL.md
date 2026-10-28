# Install, update and remove KDEGlance 2.0.0

## Binary package

The release supplies a tested CachyOS/Arch **x86_64** package. Download it from
[GitHub Releases](https://github.com/AlexGraveler/KDEGlance/releases), verify the
release checksums, then install it using the system package manager:

```bash
sudo pacman -U ./kdeglance-2.0.0-1-x86_64.pkg.tar.zst
```

Pacman resolves the declared dependencies. Close and reopen Dolphin afterward;
running Dolphin processes keep their already-loaded plugin libraries.

The preview starts on demand through the user D-Bus/systemd session. The Space
hook is loaded by Dolphin; a global KWin shortcut or custom Dolphin launcher is
not required. Space in text-entry fields and Dolphin's selection/type-ahead modes
retains its normal meaning. The context menu also offers KDEGlance: Quick Look.

## Build an Arch package from source

Install `base-devel`, `cmake`, `ninja` and `pkgconf`. Git is needed only for cloning.
Use a regular user account, not root:

```bash
git clone https://github.com/AlexGraveler/KDEGlance.git
cd KDEGlance
git checkout v2.0.0
cd packaging/arch
./make-source-tarball.sh
makepkg -si
```

Or extract `kdeglance-2.0.0.tar.gz` and run the last three commands from its
`packaging/arch` directory. The helper creates a clean local source archive and
updates its SHA-256 in PKGBUILD. `makepkg` builds Release, runs check(), then asks
Pacman to install. Do not skip the mandatory tests for release artifacts.

For a development build after dependencies are installed:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

## Dependencies

The authoritative list is `packaging/arch/PKGBUILD`.

- Qt: qt6-base, qt6-multimedia, qt6-multimedia-ffmpeg, qt6-svg,
  qt6-imageformats and qt6-5compat.
- KDE: dolphin, kio, kcoreaddons, kservice, kwindowsystem, kimageformats, kio-fuse.
- Rendering/media: poppler-qt6, LibreOffice providing `/usr/bin/libreoffice`,
  libraw, ffmpeg and hicolor-icon-theme.
- Optional formats use libheif, libavif, libjxl, openexr and openjpeg2 as supplied
  by the installed image plugins.
- Experimental MIDI requires fluidsynth plus a General MIDI SoundFont, such as
  soundfont-fluid. Without them KDEGlance reports the missing dependency.

The binary package records its build-time Qt version as a minimum runtime
dependency; source builds retain the CMake Qt 6.8 minimum. Qt plugins built against
a newer Qt minor cannot be loaded by an older Qt runtime.

Build tools: C++20 compiler/base-devel, CMake 3.22+, Ninja and pkgconf. Tests also
need D-Bus, Python, ImageMagick, qpdf and desktop-file-utils. Node.js runs the KWin
script simulation; the release validation environment includes it. The application
itself does not need Python, PyQt6, AT-SPI, pip or a virtual environment.

## Installed files

| Path | Purpose |
|---|---|
| `/usr/bin/kdeglance` | C++ viewer, resident service and CLI |
| `/usr/lib/qt6/plugins/kf6/overlayicon/kdeglance_dolphinhook.so` | Dolphin Space/order/selection hook |
| `/usr/lib/qt6/plugins/kf6/kfileitemaction/kdeglance_quicklook.so` | Context-menu action |
| `/usr/share/dbus-1/services/org.kde.kdeglance.service` | D-Bus activation |
| `/usr/lib/systemd/user/kdeglance.service` | User service |
| `/usr/share/kdeglance/kwin/placement.js` | KWin helper loaded only while the service runs |
| `/usr/share/applications/org.kde.kdeglance*.desktop` | Application identity and Settings launcher |
| `/usr/share/metainfo/org.kde.kdeglance.metainfo.xml` | AppStream metadata |
| `/usr/share/icons/hicolor/scalable/apps/org.kde.kdeglance.svg` | Icon |
| `/usr/share/doc/kdeglance/` | Documentation and license notice |

The test-only event adapter is not installed. No source/build paths, user settings
or test media are installed into the program directories.

## Updating and migration

### Existing C++ Pacman installation

Install the new package with `pacman -U` under the same `kdeglance` package name.
The update hook stops the old resident viewer cleanly; D-Bus starts the new binary
on the next request. Restart Dolphin to load new plugins. Existing C++ settings
in `~/.config/kdeglance/settings.json` remain compatible; no reset is required.

### Older manually installed Python 1.x version

The published 1.x implementation used different launchers and user integration.
The 2.0.0 package does not delete or rewrite those user files automatically.
Before switching, inspect which executable and service are selected:

```bash
type -a kdeglance
systemctl --user cat kdeglance.service
```

If a user-local or `/usr/local` launcher shadows `/usr/bin/kdeglance`, use the
uninstallation instructions/script **from that old installation** after reviewing
its scope and preserving your settings/backups. Review old manual shortcuts or
user service overrides yourself. Do not run a guessed cleanup command, remove
unrelated KDE configuration, or assume the old Python preferences have been
migrated into the C++ schema. Reopen Dolphin and verify the packaged binary with
`/usr/bin/kdeglance --version`.

## Settings and CLI

```bash
kdeglance --version
kdeglance --settings
kdeglance /path/to/photo.jpg
kdeglance --status
kdeglance --quit
```

`--reset-settings` resets the entire application; double-middle-click resets only
the current module. Settings use atomic JSON saves and recovery backup. No global
KWin mouse bindings or MIME associations are rewritten by installation.

## Removal

```bash
sudo pacman -Rns kdeglance
```

Restart Dolphin to unload its plugins. Removal stops the viewer and removes
package-owned files. User settings are preserved. Review any old manual 1.x
installation separately; removing the system package cannot uninstall arbitrary
user-local launchers.

## Clean-chroot checks

`packaging/arch/test-in-clean-chroot.sh` uses Arch devtools to build/check in a
clean chroot and test install/reinstall/removal in its disposable copy. It needs
root privileges and devtools. It must not be described as executed unless the
validation report records that run. The release also checks package lifecycle in
an isolated temporary Pacman root without removing the running desktop package.
