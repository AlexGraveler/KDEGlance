# KDEGlance v1.0.0

First public release of KDEGlance, a Quick Look-style previewer for
KDE Plasma and Dolphin, targeting CachyOS / Arch Linux with Plasma 6 on Wayland.

Highlights: image/GIF/HEIC/CR2 previews, zoom/pan and view-only transforms; audio
with embedded covers and a glass placeholder; video controls/rotation/fullscreen;
mixed-folder navigation with explicit Dolphin selection synchronization; white
vector controls; user-only installation and removal.

Local files and file URLs are direct. SMB URLs use KIO Fuse. Universal KIO/AFC
and physical iPhone previewing are not included. Fedora/Kubuntu are not officially
supported by this release. Actual codecs depend on the system backend.

Download `KDEGlance-1.0.0.tar.gz` and verify it using the accompanying SHA-256 file.
The minimal archive includes corresponding source, installation scripts and
licenses; developer tests/screenshots/media stay in the repository.
Extract, read its README.md, then run `bash install.sh` as your normal desktop user.
Reopen Dolphin and assign Space manually. The installer does not overwrite
keyboard shortcuts. The small native bridge is compiled on your machine.

GPL-3.0-or-later; see LICENSE and THIRD_PARTY.md. This is an independent project,
not an official KDE application. See TESTING.md for the
actual automated runs and outstanding native/clean-machine checks.
