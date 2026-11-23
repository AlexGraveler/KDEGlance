# KDEGlance 2.1.0

Stable release for KDE Plasma 6 / Dolphin, based on the complete 2.0.4 history.

## Added

- Automatic text syntax highlighting using KDE Frameworks 6 KSyntaxHighlighting,
  with language detection and light/dark theme matching. A setting enables it by
  default and explains the performance cost on large files.
- A shared filename overlay in every preview module, with persistent choices to
  hide it, show it at the top, or show it at the bottom.
- An audio Repeat button using the same icon and checked state as video. Audio
  and video repeat preferences are stored and reset independently.

## Improved

- Markdown remembers the last source/rendered mode selected with M across files
  and application restarts.
- Video controls hide immediately when the pointer leaves and after three seconds
  of inactivity inside the window, including paused playback. Pointer movement
  reveals them; active control dragging prevents hiding.
- Preview controls and filenames start hidden when the pointer is outside.
- Video wheel zoom resizes the window with the frame up to the work-area limit,
  then magnifies the frame inside the fixed window. Reverse zoom removes the
  internal magnification before shrinking the window. Temporary zoom does not
  overwrite remembered manual window bounds.
- A newly approved, darker vector audio placeholder improves control contrast,
  fills arbitrary window proportions and keeps the note centered without a
  separate bottom darkening gradient. Existing cover artwork is preserved.
- Filenames use one consistent, background-free overlay with a 20% larger font;
  duplicate audio and information-card filenames are removed.
- Settings remain on one page with native Plasma colors, fonts and widget style.
  Video repeat is controlled by its existing player icon rather than a redundant
  settings checkbox.
- LibreOffice is optional for package installation, building and testing. Only
  tests that actually need Office conversion skip when it is absent.

## Fixed

- Clear existing syntax colors when highlighting is disabled and refresh the
  highlighting theme when the palette changes.
- Prevent load/playback callbacks and keyboard activity outside the window from
  revealing controls unexpectedly.
- Keep repeat state and independent module reset behavior consistent when
  switching between audio and video or applying settings.
- Preserve the 2.0.4 fix that renews stale KWin geometry waits after image
  navigation, and its deferred decoder-error cleanup with stale-error guards.
- Retain 2.0.3 coordinated initial image/video geometry, fractional-scale and
  cursor-anchor fixes, and the PDF scrolling improvements from 2.0.1/2.0.2.

## Installation and compatibility

Download the package for your distribution and verify SHA256SUMS. Arch/CachyOS
users can also build the pinned 2.1.0 source with the supplied PKGBUILD and
kdeglance.install using `makepkg -si`; LibreOffice is not required. Install
LibreOffice separately only for supported Office document previews.

The CachyOS binary is x86_64-v3 and requires Qt >= 6.12.0 plus compatible rolling
libraries. Build from source on older CPUs or Qt versions. RPMs, when included,
are distribution-specific and unsigned; use the Fedora version in their filename.
Restart Dolphin after a package upgrade to load its new plugins. Settings remain
compatible. No global KWin bindings or MIME defaults are changed.

## Validation

The project owner reports all 18 manual acceptance checks passed on the accepted
pre-release build. Final automated results, package inspection and platform limits
are recorded in the accompanying VALIDATION.md. The release integrates 2.0.4 as
an ancestor; no existing tags or releases are replaced.
