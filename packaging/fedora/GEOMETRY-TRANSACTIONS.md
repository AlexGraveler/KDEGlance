# Geometry transactions on Plasma Wayland

Local development verification, 2026-11-10. No GitHub publication or upstream
release. This supersedes the resize/move mechanism described in
[the previous zoom report](ZOOM-VALIDATION.md).

## Established cause

The old path painted a new photo size before the KWin D-Bus response moved the
window. The baseline desktop trace recorded client resize/paint at
1793974153886/3888, a compositor size change at the old origin at 3890, and a
separate move at 3893. A correct final cursor position could therefore pass while
the user saw two different intermediate positions. This was an application
coordination error, not evidence of a Fedora-specific KWin bug.

The initial media path also exposed the widget before asynchronous dimensions
and the final configure were settled. Probing dimensions and scheduling another
resize did not prevent the earlier buffer from being visible.

Wayland clients do not choose their own global origin. KWin's XdgToplevelWindow
moveResizeInternal/handleCommit implementation applies a combined move/resize
position when the matching client buffer is committed. The solution uses this
existing compositor transaction rather than a timed post-resize correction.
References: [xdg-shell protocol](https://wayland.app/protocols/xdg-shell),
[KWin Plasma 6.6 source](https://invent.kde.org/plasma/kwin/-/blob/Plasma/6.6/src/xdgshellwindow.cpp).

## Common implementation

- The existing runtime-loaded KWin helper waits for a geometry request before
  resizing. One complete frameGeometry assignment carries size and position.
- The viewer commits zoom only after both the corresponding configure/resize
  and the request confirmation. If configure arrives first, updates are held;
  the resized backing store cannot expose the previous transform.
- One transaction is in flight. Further wheel events accumulate the desired
  zoom and are dispatched after its commit. At the screen-size cap, zoom is
  local and needs no window transaction.
- First map uses a transparent surface for configure negotiation. The media
  widget, corners and shadow are exposed only after dimensions, readiness and
  geometry agree. File switching retains the previous image while staging.
- Video dimensions can come from QVideoSink before playback; autoplay disabled
  does not require playing a frame just to establish geometry.
- Request IDs reject stale replies after switching/closing. KWin listeners are
  bound to the preview window and caption. Manual movement/managed geometry
  reject pending wheel transactions. A missing compositor confirmation produces
  an error card after five seconds; that timeout never commits media geometry.
- Wayland uses the coordinator when the existing KWin helper is available.
  X11 and other platforms retain the client placement path. There is no Fedora
  fork or distro-dependent geometry calculation. Aspect ratio is preserved.

## Verification

- Development CTest: 16/16 passed, including stale replies, configure before
  confirmation, ten coalesced wheel events, autoplay-off video, managed-window navigation and seven aspect
  cases (landscape, portrait, square and very wide content).
- Fractional scale checks at 1.25, 1.5 and 2: 12/12 QtTest results at each scale.
  These are offscreen tests, not a claim of physical mixed-monitor testing.
- Actual Fedora 44 Plasma Wayland at 125%: four initial Fit cases and 24
  cursor-anchor checks passed. The measured logical anchor error was zero.
  Each photo wheel input generated one compositor geometry change; video zoom
  generated none, as expected for its fixed window.
- A portal-authorized PipeWire capture from 2026-11-06 contains 116 actual compositor frames
  of generated photo/video content on a neutral background. Frame inspection
  showed no intermediate side bars. The 12 photo size transitions preserve the
  predicted raster anchor to at most 1.94 physical pixels (rounding); no frame
  exposed the new size at the old origin. This is a captured desktop sequence,
  not physical mouse/touchpad certification or a claim that every display
  refresh was sampled.
- Additional actual-desktop screen-cap round trip: 48 cursor measurements
  passed, with single combined geometry changes during window growth and no
  geometry changes once the viewport reached its cap.
- Capture and local traces: build/desktop/sequence/. Recordings from earlier
  cancelled/invalid harness attempts are retained but excluded from results.
- The SMB server was unavailable (KIO Fuse connection timeout). The user asked
  to proceed without it. Real SMB playback/switching is therefore unverified
  in this iteration. Existing remote resolver tests cover cancellation, stale
  mount replies, KIO copy cleanup and network errors. The desktop script accepts
  repeatable --smb URLs for later live checks; remote media cannot be recorded
  with its --record option.
- No actual Arch/CachyOS system was available. Its packaging layout and common
  X11/client geometry code remain covered locally; this does not replace a
  clean Arch build or manual acceptance there.

## Installed RPM

- Built the final `kdeglance-2.0.2-3.fc44.x86_64` from a fresh source extraction;
  its complete `%check` passed 16/16. Production source files match the working
  tree. Test-driver/documentation additions after this build are included in
  the final source RPM and do not change the installed executable.
- Upgraded only KDEGlance from release 2 to release 3 through DNF/Polkit.
  `rpm -V kdeglance` passed; the fixed package remains installed.
- Repeated the desktop sequence using `/usr/bin/kdeglance` and the installed
  KWin helper: all four initial fits, every initial content paint, 24 cursor
  checks and compositor-change counts passed. The additional `--installed
  --cap` run passed 48 cursor checks through the screen limit and back.
- Installed Dolphin integration passed: systemd/D-Bus activation, Space, both
  supplied files, arrow navigation, selection synchronization, closing and
  context-menu preview. Dedicated Dolphin test processes were closed afterwards.
- The user's settings.json SHA-256 was unchanged across installation and tests.
- LibreOffice remains optional at runtime. RPM requirements do not include it.
- rpmlint: zero errors, three pre-existing warnings (documentation size, absent
  man page and local Source0). The local RPM is unsigned.

Logs and package checksum are retained in `build/geometry-validation/`.
Physical-input manual acceptance, actual Arch/CachyOS validation and live SMB
checks remain explicitly outside these passing results. No changes were pushed.

