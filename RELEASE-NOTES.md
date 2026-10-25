# KDEGlance v1.0.1 — local release candidate

This maintenance release preserves image/GIF/audio/video rendering and controls.
It fixes migration diagnostics and retired experimental menu handling.

- Diagnose PATH shadowing by the old /usr/local/bin/kdeglance launcher. Packaged
  --version and state accept no image path; --version works during migration.
- Back up recognized retired log_selection.py menus, and avoid restoring them
  to active Dolphin integration when uninstalling a manual installation.
- Preserve unknown files, shortcuts, media preferences and existing backups.
- Add CLI and migration regression tests, and local Arch/CachyOS packaging 1.0.1-1.

KDEGlance-1.0.1.tar.gz is a minimal source/install archive with GPL-3.0-or-later
and required notices. The local PKGBUILD consumes that real local archive and its
computed SHA-256. No GitHub/AUR publication or live update is performed here.
After owner approval and actual upload, a publisher may verify the remote asset
and then change the AUR source URL; do not point it at an unpublished release.

Read README.md/INSTALL.md and the package migration guide before replacing a
manual installation. Native GUI acceptance of 1.0.1 remains a test-laptop step.
