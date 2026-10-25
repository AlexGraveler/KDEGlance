# Changelog

## [1.0.1] — 2026-10-24

- Diagnose PATH shadowing by the retired /usr/local/bin/kdeglance launcher; preserve CLI argument forwarding and allow --version during migration.
- Back up only recognized retired LogSelection experiment menus; do not reactivate them on manual uninstall. Preserve unknown files, settings and original backups.
- Add CLI, migration and settings-preservation regression checks.
- Prepare a local 1.0.1-1 Arch package with real source checksums; no unpublished GitHub download is assumed.

## [1.0.0] — 2026-10-24 — Initial public release

- Quick Look-style user-level Dolphin integration with manual Space binding.
- Borderless image preview, EXIF orientation, zoom/pan, rotation and reflection.
- Bounded image decoding and buffered playback of large/long GIF animations.
- Audio playback with embedded artwork, glass-style fallback, seek and volume.
- Natural-aspect video preview, playback controls, rotation, zoom and fullscreen.
- Mixed image/audio/video navigation with explicit Dolphin order/current-item sync.
- Local/file URL paths and SMB access through KIO Fuse.
- Shared fading controls, movable windows, keyboard navigation and focus closing.
- Reference-style white vector icons, preserving existing control actions.
- Source-only Arch/CachyOS installer, local native bridge build, reversible user
  integration and repeatable manifest-based removal.
- Original redistributable media fixtures, automated tests, screenshots and GPL licensing.

Earlier internal development iterations were not public releases. No fictional
public release history is included.

### Installer corrections before publication

- Bootstrap dependency checks without importing application modules or requiring
  PyQt6. Read the release version using only the standard library.
- Defer Qt/Gio runtime checks until missing system packages have been installed;
  list missing packages without a traceback in read-only check mode.
- Cover clean-machine PyQt6/Qt Wayland combinations and confirmed package setup
  with isolated installer regression tests. Preview/playback behavior is unchanged.
