# Testing KDEGlance 1.0.0

## Automated checks

Run as a normal user from this source tree. Install the normal runtime/build
requirements first. Optional development packages are `python-pillow`, `ffmpeg`,
`libheif` and `desktop-file-utils` (desktop-entry validation); these are not normal application dependencies. No Codex is needed.

```bash
bash -n install.sh uninstall.sh
bash install.sh --check
python3 -B tests/run_tests.py
python3 -B tests/run_tests.py --gif
python3 -B tests/run_packaging.py
python3 -B tests/render_screenshots.py
python3 -B tests/verify_release.py
python3 -B packaging/build_release.py
python3 -B tests/verify_archive.py
```

The viewer/GIF runners create a marked disposable HOME/XDG environment under
`.work/`, use a private `dbus-run-session` and Qt's offscreen platform, and build
the native bridge locally. Packaging tests use disposable homes including spaces
and Cyrillic characters. They never call the real user service manager to install
or remove the application. The hidden `--test-isolated` mode requires a marked
root and refuses the real account home. Do not run lower-level test files directly
in a working desktop session. `install.sh --check` only reads requirements/session
state; it does not install packages or modify integration.

- 55 viewer tests cover image decoding, GIF timing, EXIF/HEIC, corrupt inputs,
  zoom/pan, transforms, audio metadata/playback/seek/volume, video frames/rotation,
  module switching, bounded caches, D-Bus lifecycle and mocked Dolphin order/current
  item synchronization. Two historical installer assertions were updated to the
  release policy of refusing unowned service-menu files; backup restoration is
  tested with a pre-existing custom Dolphin launcher instead.
- 5 GIF tests cover streaming beyond the old frame limit, finite loops, delays,
  transforms, backpressure/cancellation and worker errors. A generated animation
  exceeding the old decoded-memory budget is played at full quality through a loop.
- 12 packaging tests cover install/update/uninstall, repeat removal, version and
  required files, shortcut preservation, custom launcher restoration, scoped
  preferences, changed files, malformed ownership, symlinks, unsupported systems
  and noninteractive dependency refusal.
- `verify_release.py` checks source syntax, versions, documentation links, public
  file hygiene and redistributable assets. Results should be recorded locally, outside the user archive.

The WMV/FLV/RMVB fixtures exercise WMV2/WMAV2, FLV1/MP3 and RV20/RealAudio144.
They do not prove every WMV/FLV/RealVideo variant works. Extension support is not
an exhaustive codec guarantee. CR2 is accepted through installed Qt plugins; no
redistributable real camera CR2 fixture is included in this release.

## Original test media and screenshots

The examples are kept only in the repository and are not in the user archive. To
regenerate them, run `python3 -B tests/generate_fixtures.py`. This replaces only
the source tree's synthetic examples; encoding bytes may vary across versions.
See tests/FIXTURES.md and screenshots/README.md for provenance. The fixture
rotation uses FFmpeg's `-display_rotation` input option; regeneration needs an
FFmpeg build providing that option. No real NAS/media library is used by tests.

## Manual checks before publication

These require a separate disposable KDE user account or machine. They have not
been rerun against the owner's working installation during release preparation.

1. On clean Arch/CachyOS Plasma Wayland, run the installer from a path containing
   spaces. Confirm missing packages are proposed, refusal leaves integration
   untouched, and acceptance builds/starts version 1.0.0 without running as root.
2. Reopen Dolphin, bind Space manually after reviewing conflicts, and preview
   JPEG/PNG/GIF/HEIC/CR2, audio with/without embedded art, and diverse video codecs.
3. Navigate a mixed folder in Dolphin's visible sort order. Close and use arrows
   in Dolphin: both the selected item and keyboard current item must stay on the
   last preview. Check explicit clicks, Space/Escape and focus loss.
4. Check window dragging, small/high-DPI screens and multiple monitors, image and
   video pointer zoom/pan, rotation/reflection, fullscreen, hover-only controls,
   fade timing, progress wheel seeking and volume wheel handling over the speaker.
5. Open SMB media using an authenticated test share through KIO Fuse, including
   non-ASCII paths and unavailable servers. Do not put credentials in reports.
6. Restart the user session; check D-Bus activation/autostart and manual Space.
   Remove twice; check owned files disappear, custom launcher/preferences remain,
   and unrelated applications/shortcuts/packages are untouched.

Screenshots captured offscreen demonstrate real widget rendering only. They do
not replace KWin placement, native activation, real Dolphin accessibility,
physical NAS/CR2, hardware audio output or clean-distribution acceptance tests.
Fedora/Kubuntu/X11 and universal AFC/iPhone KIO are outside this release scope.

## User archive

`python3 -B packaging/build_release.py` creates KDEGlance-1.0.0.tar.gz plus its
SHA-256 sidecar from the explicit `packaging/distribution.py` allowlist. Run
`tests/verify_release.py` separately to audit the repository.
Only the explicit application/install/license allowlist is included. Tests,
screenshots, examples, developer documentation, reports and internal source
checksums are excluded, along with native binaries and private test work.
`tests/verify_archive.py` uses the repository fixtures externally to verify the
extracted program; no media is added to the user archive.
The archive has normalized ordering, ownership and timestamps; identical source
bytes produce identical archive bytes. Media regeneration across codec versions
is not claimed to be bit-identical.
