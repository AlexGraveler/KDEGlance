# KDEGlance 2.0.2

## Bug fix: slow PDF wheel scrolling

Fixed insufficient vertical mouse-wheel scrolling speed in the PDF viewer.
Scrolling is now **twice as fast as 2.0.1**, or **54x relative to 2.0.0**, in both
page and text-reflow modes. Ctrl+wheel zoom and horizontal scrolling are unchanged.
Office previews and all other viewers retain their previous behavior. No new
controls, shortcuts, settings or window-geometry changes are introduced.

The Arch package is **2.0.2-1** and uses the official v2.0.2 source asset with a
pinned SHA-256. LibreOffice remains optional at runtime; complete Office tests
require it as a check dependency.

The source archive contains application sources, data, tests and documentation.
Arch recipes are maintained separately in the repository's packaging/arch folder,
so the archive does not need to contain its own checksum.

Any supplied CachyOS-built binary requires **x86-64-v3, Qt >= 6.12.0 and compatible
current system libraries**. For baseline x86_64 or an older compatible Qt version,
build from source in an appropriate Arch environment.

Existing v2.0.1 and v2.0.0 tags, releases and assets are preserved unchanged.
