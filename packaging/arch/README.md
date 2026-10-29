# Arch Linux / CachyOS / EndeavourOS

This recipe builds **KDEGlance 2.0.1-2** from the official v2.0.1 release asset:

https://github.com/AlexGraveler/KDEGlance/releases/download/v2.0.1/kdeglance-2.0.1.tar.gz

SHA-256 is pinned in PKGBUILD. The current `main` source tree is not compiled.
`pkgver=2.0.1` and `pkgrel=2` identify this release package.
The package architecture is x86_64; ARM builds have not been validated.

## Install locally

Run makepkg as a regular user, not root:

```bash
sudo pacman -S --needed base-devel git
git clone https://github.com/AlexGraveler/KDEGlance.git
cd KDEGlance/packaging/arch
makepkg -si
```

Use the current packaging files, not the older recipe included in the v2.0.0 tag.
Do not run `make-source-tarball.sh` before this workflow: the source must come
from the official release URL and match the pinned checksum. The PKGBUILD,
.SRCINFO and kdeglance.install can also be copied into a separate build directory.

makepkg downloads/verifies the source, resolves build/runtime/test dependencies,
builds and runs the complete CTest suite, then asks Pacman to install. No files
are installed into the user's home directory. Close and reopen Dolphin after
installing/upgrading to load both plugins. The viewer starts on demand via D-Bus;
no system-wide service enablement or global shortcut changes are needed.

To build without installing:

```bash
makepkg -s
```

To install an already built package:

```bash
sudo pacman -U ./kdeglance-2.0.1-2-x86_64.pkg.tar.zst
```

## Dependencies and integration

LibreOffice is an **optional runtime dependency** for Office previews (DOC, DOCX,
ODT and other supported formats). The virtual `libreoffice` dependency is provided
by libreoffice-fresh or libreoffice-still. Without it, Office previews show a
clear dependency message; images, PDF, text, audio and video still work.
The upstream tests generate/convert Office documents, so LibreOffice remains in
`checkdepends`: building with the complete tests requires it, installing the
resulting binary package does not.

PKGBUILD separates runtime dependencies, makedepends, checkdepends and optdepends.
KIO Fuse supports remote streaming; kio-extras supplies SMB and additional
protocols. Optional image libraries enable additional formats; experimental MIDI
needs FluidSynth and a SoundFont. The application itself does not require Python.

Pacman owns the executable, two Dolphin plugins, desktop files, D-Bus activation,
user systemd unit, KWin helper, icon and documentation. The existing install
script stops running KDEGlance processes on upgrade/removal and preserves user
preferences. Dependencies are system packages and are not bundled.

## Validation and binary compatibility

```bash
makepkg --verifysource
makepkg --printsrcinfo > .SRCINFO
namcap PKGBUILD kdeglance-2.0.1-2-x86_64.pkg.tar.zst
```

Install namcap separately if needed. Do not skip the tests to produce release
artifacts. The recipe respects makepkg flags; CachyOS may use `-march=native` or
supply x86-64-v3 startup objects even with generic compiler flags. Use a standard
Arch x86_64 chroot for binaries intended for baseline CPUs. package() records the
Qt version used to build the Dolphin plugins as a minimum runtime dependency.
Other system libraries must also be compatible and up to date.

## Future releases and AUR

1. Set pkgver to an actually published release and reset pkgrel=1.
2. Keep the URL pinned to `releases/download/v${pkgver}/...`.
3. Fetch and review the source. Update SHA-256 manually or use updpkgsums from
   pacman-contrib. Never use SKIP.
4. Build with checks, inspect the package and regenerate .SRCINFO.
5. An AUR submission should contain PKGBUILD, .SRCINFO and kdeglance.install;
   exclude binary packages, downloaded archives, build directories and logs.
   Set the maintainer contact before submission.

These instructions do not publish to AUR or alter the v2.0.0 tag/GitHub Release.
The existing 2.0.0-1 release binary is not replaced by this packaging revision.
