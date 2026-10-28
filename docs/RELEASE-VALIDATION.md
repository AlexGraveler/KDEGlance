# KDEGlance 2.0.0 release validation

Release target: CachyOS / Arch Linux, KDE Plasma 6, Dolphin, KWin, Wayland,
x86_64. License: **GPL-3.0-or-later**, explicitly selected by the project owner.
This is release preparation of the accepted C++ application, not a new interaction
implementation.

## Stable baseline and history

The local stable checkpoint is `stable-before-v2.0.0-20261028` (accepted photo
plain-pan/Alt-window-move behavior). All **64 runtime C++/header/JavaScript files**
match that checkpoint after normalizing the owner-approved SPDX license change.
Application IDs, MIME lists, D-Bus interfaces and settings keys/schema are retained.
Version changes are CMake/VERSION, plugin metadata, AppStream and package metadata.

The public repository's v1.0.0/v1.0.1 tags preserve the earlier Python/PyQt6
implementation. Its README/source explicitly excluded text/PDF and already
implemented image/audio/video preview. The release notes distinguish retained
features from the expanded C++/document release; they do not claim that image zoom,
rotation or media preview first appeared in 2.0.0. Existing public history is
preserved through a normal descendant commit, without a force push.

## Environment

- CachyOS; one available screen, 2560×1440, reported work area 2560×1402.
- Plasma 6.8.1, KWin 6.8.1, Dolphin 26.08.2.
- Qt 6.12.0; KIO/KCoreAddons/KService/KWindowSystem 6.31.0.
- Poppler Qt6 26.10.0, FFmpeg 9.0.2, GCC 16.2.1.
- The binary package records its build-time Qt version as its minimum Qt runtime,
  respecting Qt's plugin minor-version compatibility rule. CMake's source minimum
  remains Qt 6.8. See [Qt plugin compatibility](https://doc.qt.io/qt-6/deployment-plugins.html).

## Confirmed feature inventory

| Module | Code inspected | Confirmed functionality / evidence |
|---|---|---|
| Shared/Dolphin | Bridge, DolphinAccess, ViewerApp, PreviewWindow | Space/context-menu integration, Escape/Space close, arrow navigation, source selection/session ownership, frameless windows; integration/desktop tests |
| Default application | DefaultFileOpener, PreviewWindow key handler | Enter and keypad Enter close/stop before KIO launch; original URL retained; duplicate/self-launch/error handling; unit and desktop capture tests |
| Photo/animation/RAW | ImageView, Geometry, ImageLoader, AnimationPlayer, rawdecoder | Natural/saved fit, cursor-constrained zoom, window growth to screen then crop zoom, plain-pan/Alt-window-move, free/Alt-ratio resize, temporary zoom vs manual persistence, transforms, animation/TIFF/RAW; source comparison and renderer/window/format/desktop checks |
| Video | MediaView, VideoSurface, MediaControls | Decoded frames, play/pause, seeking, volume/mute, fullscreen, one counterclockwise rotation button, zoom/pan, shared side navigation, software decoding fallback |
| Video Repeat | repeatPlayback button → loopToggled → video module setting → EndOfMedia restart | Button visibility/toggle checked in real Wayland; unit test clicks the actual button, verifies restart, turns it off and verifies stop |
| Audio | MediaView metadata/artwork path and controls | Play/pause, seeking, volume/mute, embedded/adjacent artwork or placeholder, compact player, navigation, stop-on-close and independent geometry/preferences |
| Text/Markdown | TextView and format detection | UTF-8/UTF-16, Cyrillic/Western legacy heuristics, read-only plain formats, Markdown source/rendered view, chunking and doubled scrolling; mandatory document tests and six extra plain-format probes |
| PDF | PdfView/Poppler | Continuous pages, scroll, asynchronous bounded raster cache, zoom/fit-width and text-layer reflow; document and real-window checks |
| Writer documents | OfficeView | DOC/DOCX/DOCM/RTF/ODT/OTT/FODT converted in isolated headless LibreOffice profile then displayed as PDF; real generated fixtures and cancellation tests |
| Settings | Settings and active-module reset | Independent sizes for six modules; audio/video scoped volume/mute and video Repeat; double-middle reset preserves other groups after restart; schema/keys unchanged |
| Remote | RemoteResolver, DefaultFileOpener | KIO Fuse attempt/cancellable copy, original-URL dispatch; private-server/transport tests. No new live SMB/AFC certification is claimed |

## Build and automated results

A **fresh Release build with standard Arch makepkg** and all installed Qt/KF
libraries completed. All **16/16 CTest sets passed**, total **83.04 seconds** in
the final licensed build. No mandatory test/format was disabled or removed.

The sets cover settings, geometry, types/navigation, image/media rendering,
window/service behavior, stress, real decoded formats, Dolphin integration,
KWin simulation, AV1 software fallback, documents/conversion, remote resolution
and Enter/default opening. The KWin simulation has **15 passing scenarios**.
The additional Repeat UI test uses its own settings to avoid contaminating the
older loop test; application playback code was not changed to make it pass.

The release-candidate binary decoded **97 media fixtures with zero failures and
zero mandatory failures**. MIDI used an existing temporary FluidR3 SoundFont via
a test environment override; the release did not install a SoundFont globally.
Separate CSV, INI, JSON, XML, YAML and YML samples all displayed: **6/6**.
Mandatory document fixtures include TXT/LOG/MD, Unicode/legacy text, PDF and all
seven advertised Writer extensions.

## Actual desktop windows

The extracted release candidate, its own Dolphin plugins and KWin helper ran
against the live user Wayland session with separate settings and synthetic files:

- **120/120** photo/Dolphin/KWin/reset checks. Includes final plain-pan/Alt-window
  actions, cursor anchoring after reset/reopen/move, high-resolution/reversed wheel,
  manual size restoration, actual maximize/left/right tile/restore, Repeat toggle,
  all six independent resets, process restart and mixed open/close cycles.
- **61/61** native-window Enter/KIO checks with isolated associations across image,
  video, audio, text, PDF and all seven Writer extensions. Original arguments and
  close/stop-before-launch are checked; user MIME preferences are untouched.
- **26/26** checks with actual user default Gwenview, Kate and VLC windows for
  image/text/video. Both Enter variants are used. An exploratory audio window
  matcher used an ambiguous MIME alias and was not counted as a successful GUI
  launch check. All-module dispatch is covered separately above. Existing audio,
  PDF and office associations are not replaced merely to obtain a passing test.
- Real widget screenshots were refreshed with synthetic content, including PDF,
  text and Writer. They were visually inspected; no private documents are shown.

These counts verify handlers in real Qt Wayland windows and actual KWin states,
using an explicit test-only Qt event adapter. They **do not** represent physical
Wayland seat injection or subjective smoothness approval. The adapter is not in
the installed package. Test windows are closed and the original service restored.

## Packaging and distribution checks

- Package identity: `kdeglance 2.0.0-1`, binary architecture x86_64. No untested
  distribution/architecture binaries are supplied.
- Declared runtime dependencies are satisfied on the host; no app IDs, activation
  paths or MIME mappings were renamed.
- Disposable-root Pacman file/database installation of 1.0.0-5, upgrade to 2.0.0-1,
  integrity inspection and removal passed. Runtime dependencies and maintainer
  scripts are intentionally disabled **only in this empty file-test root**; that
  test is not represented as a complete clean-chroot/runtime installation.
- AppStream and desktop metadata validate. Desktop validators emit only existing
  category hints; there are no validation errors. The viewer has no build-directory
  RPATH/RUNPATH.
- Source export is allow-listed. It excludes local settings, caches, private Git
  history, development reports, generated media libraries, logs, tokens and build
  outputs. Current tests, intentional AV1/FODT fixtures and UI PNGs are included.
  Archive path/link/credential-pattern checks and version consistency are run.
- Full GPL-3.0-or-later text is supplied in LICENSE/COPYING and installed notices.
  Codecs, Qt/KDE, LibRaw, Poppler and LibreOffice remain system dependencies.
- Release assets include the source archive, Arch package and SHA256SUMS. They are
  unsigned; checksums verify content and are not a signing-key certification.

Final live installation/publication results are recorded in the GitHub release
validation section; this frozen build report distinguishes the candidate checks
from that final transaction. Only documentation/packaging metadata may be refreshed
after successful code tests; executable/plugin/KWin payload hashes are compared
when doing so.

## Limits and manual acceptance

No independent clean Arch machine/devtools chroot, other distribution, second
monitor or mixed-DPI hardware was available. Physical mouse/touchpad smoothness,
electric-edge triggers, every Alt/Meta binding combination and audible long-session
acceptance remain in [the manual checklist](MANUAL-ACCEPTANCE.md). KWin's global
bindings/settings were not changed. The established stable interaction behavior
was preserved rather than redesigned during release work.

No newly authorized SMB server URL was supplied for this release run. Network
unit fixtures and original-URL handoff are not presented as live SMB/AFC/iPhone
hardware certification. Legacy text detection is heuristic; PDF reflow is not OCR;
LibreOffice import fidelity and exact camera/codec variants remain conditional.

Raw local evidence is retained outside the distributable source archive in the
release build folder: makepkg logs, runtime-preservation audit, desktop/default-open
results, media/text probes, environment, archive audit and Pacman lifecycle report.
