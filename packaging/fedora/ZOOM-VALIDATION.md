# Initial geometry and cursor zoom — 2026-11-06

Local Fedora fix, packaged as `kdeglance-2.0.2-2.fc44.x86_64`.

## Causes and changes

- Saved image/video rectangles were restored independently of the next file's
  aspect ratio. The initial window now fits the content inside the saved bounds;
  the stored preference is retained. Free resizing still works on the current
  file, and navigation starts the next photo/video with its own aspect ratio.
- Expanding video width and height independently to the minimum window size
  introduced unnecessary bars. Automatic sizing now scales both axes together.
- Rounding natural dimensions immediately after DPI conversion changed their
  aspect ratio at fractional scale factors. `naturalSizeF()` retains fractional
  dimensions until the final window size is rounded.
- The first video's dimensions were published from `paintEvent`, allowing
  window geometry changes while a frame was being painted. Frame conversion and
  size notification now happen before painting, including view rotation.
- Photo zoom snapped to Fit inside a 2% band. That discontinuity is removed;
  exact Fit remains available. Unchanged scale at a limit leaves placement alone.
- Photo window resizing requires screen anchoring as well as local image
  anchoring. Wayland clients do not provide reliable window origins. The KWin
  helper composes the viewer's unapplied affine transforms around its own cursor
  position, accepting fractional compositor rounding. It rejects stale session,
  size and sequence results and discards pending work on manual interaction or
  managed-window transitions. X11 client moves are not applied twice.
- Photo and video share `Geometry::zoomAt` and `Geometry::clampPan`. Aspect ratio
  is retained without stretching or cropping. Centering/clamping still applies
  when the content is smaller than the viewport or reaches its pan boundary.

## Verification

- Full development CTest: **16/16 passed**.
- KWin scripting simulation: **20 scenarios passed**, including native cursor
  coordinates, X11 placement, coalesced wheel events and stale replies.
- Geometry cases with `QT_SCALE_FACTOR=1.25`, `1.5`, `2`: **7 passed at each
  scale**, covering both orientations of images/video and photo-window anchoring.
- Real Plasma Wayland on the current **125%** screen: four initial Fit checks
  and **24 cursor-anchor measurements passed**, for landscape/portrait PNG and
  MPEG-4 video. These used actual KWin window geometry, synthetic content and the
  development Qt event adapter. Screenshots were inspected for unintended bars.
- The Arch `lib/qt6/plugins` CMake layout remains valid; the Arch PKGBUILD and
  release checksum are unchanged. No actual Arch/CachyOS build was performed.

The checks use generated files, not personal photos or videos, and do not change
monitor scale, global shortcuts or MIME associations. Physical mouse/touchpad
smoothness remains for manual acceptance. The previously investigated
missing-codec crash handler was not changed in this UI task.

## Installed RPM

- Clean Fedora RPM build and its full `%check`: **16/16 passed**.
- Upgraded the host from `2.0.2-1.fc44` to **`2.0.2-2.fc44`**, leaving the fixed
  version installed for manual acceptance. Only KDEGlance was upgraded.
- `rpm -V kdeglance` passed outside the execution sandbox.
- Repeated `check-zoom.py --installed` using `/usr/bin/kdeglance` and the installed
  KWin helper: all four initial Fit cases and 24 cursor measurements passed.
  The reported coordinate error was zero in those measurements; this does not
  imply subpixel physical-device certification.
- Repeated the installed Dolphin smoke check: Space, context-menu action,
  navigation and selection synchronization all passed.
- The SHA-256 of the existing user settings file was unchanged after installation
  and testing. Test configuration and media were kept under `build/desktop/zoom`.
- `rpmlint`: zero errors; the three existing warnings concern documentation size,
  the absent man page and local Source0. The RPM remains unsigned.

Logs are retained locally in `build/zoom-validation/`; screenshots are in
`build/desktop/zoom/`. No GitHub push, tag, PR or release was created.
