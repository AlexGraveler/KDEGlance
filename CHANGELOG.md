# Changelog

## 2.0.2 — 2026-10-29

- Fix slow vertical PDF mouse-wheel scrolling: double the speed of 2.0.1
  (54x relative to 2.0.0), in page and text-reflow modes.
- Preserve Ctrl+wheel zoom, horizontal scrolling and all other viewers.

## 2.0.1 — 2026-10-29

- Increase vertical mouse-wheel scrolling twenty-sevenfold in the PDF viewer, including text reflow.
- Preserve Ctrl+wheel zoom, horizontal scrolling and all other viewers, including
  the PDF component embedded in Office previews.

## 2.0.0 — 2026-10-29

This release packages the accepted C++20 / Qt6 / KF6 implementation without
changing its final runtime behavior. See [full release notes](RELEASE-NOTES.md).

- Add text/Markdown, continuous PDF and LibreOffice Writer document previews
  relative to the published Python 1.0.1 feature set.
- Open original local/remote URLs with KDE MIME associations through Enter/keypad
  Enter, closing the preview and stopping its playback first.
- Include independent module geometry/preferences and active-module middle-double
  reset; preserve settings compatibility with the local 1.0.0-5 C++ packages.
- Document accepted photo behavior: ordinary drag pans a zoomed photo, Alt-drag
  moves its window; temporary wheel zoom and manual saved geometry stay separate.
- Include video Repeat, counterclockwise rotation, fullscreen, zoom/pan and shared
  navigation. Repeat is explicitly documented and retained.
- Ship native Dolphin plugins, D-Bus/systemd activation, KWin placement helper,
  updated English documentation, release source archive and tested Arch package.

## Published 1.x history

The GitHub tags `v1.0.0` and `v1.0.1` preserve the previous Python/PyQt6 source and
its original release documentation. Version 1.0.1 was a maintenance/migration and
packaging update. Image/GIF/audio/video preview, rotation, zoom and navigation
already existed in that implementation; they are not claimed as first appearing
in 2.0.0.

Local C++ development checkpoints named 1.0.0-2 through 1.0.0-5 were acceptance
builds, not additional published GitHub releases. Their accepted behavior forms
the stable baseline of 2.0.0.
