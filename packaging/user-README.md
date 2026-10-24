# KDEGlance 1.0.0

Quick Look-style image, audio and video previews for KDE Plasma and Dolphin.
Supported setup: CachyOS / Arch Linux, Plasma 6 Wayland, Dolphin 24.02+,
Python 3.10+, Qt/PyQt 6.8+. Fedora/Kubuntu are not officially supported.

## Installation

Extract the archive, open a terminal in the extracted directory and run as your
normal desktop user:

```bash
bash install.sh --check
bash install.sh
```

The installer checks runtime dependencies, proposes missing packages through
`sudo pacman -Syu --needed` with confirmation, and builds the small native
activation bridge from the included source. It installs into your user account;
do not run the whole installer with sudo. A compiler/pkg-config is required
only for this local build, not for normal playback. No pip, test media, test
framework, Git or Codex is needed.

It creates KDEGlance's user service, D-Bus activation and Dolphin action, and
backs up a user Dolphin launcher to enable accessibility for file navigation.
Existing unrelated integration files are refused. Keyboard shortcuts are not
rewritten. Existing KDEGlance integration is updated only after confirmation.

Reopen Dolphin. In **Settings → Configure Keyboard Shortcuts**, search for
**KDEGlance: Quick Look** (or its translated name) and assign **Space** after
reviewing conflicts. Select a supported file and press Space. Arrow keys navigate
mixed media; Space/Escape or focus loss closes the preview. Use the on-screen
controls, the wheel over the progress bar to seek, and over the speaker to adjust
volume. Photo/video wheel zooms; dragging zoomed content pans it.

## Removal

```bash
bash uninstall.sh
```

Removal stops KDEGlance's service, restores its backed-up integration and keeps
preferences, logs/backups and shared system dependencies. Repeated removal is safe.
To also remove only KDEGlance audio/video preferences, with confirmation:

```bash
bash uninstall.sh --purge-settings
```

## License and source

GPL-3.0-or-later. See LICENSE and THIRD_PARTY.md. This archive includes the Python
application, native C++ bridge source, build/install scripts and bundled Mutagen
source with its upstream notices. Qt/KDE binaries are system dependencies.
Control icons and the cover placeholder are drawn by the included application code.

Full documentation, screenshots and developer tests are available in the repository:
https://github.com/AlexGraveler/KDEGlance

Independent project; not an official KDE application. Format/codec availability
depends on installed Qt plugins/backend. SMB uses KIO Fuse; universal KIO/AFC,
PDF and text previews are outside version 1.0.0.
