# KDEGlance 2.0.1

This maintenance release makes one functional change: vertical mouse-wheel
scrolling in PDF previews is twenty-seven times as fast as in 2.0.0, in both page and text-reflow
views (a further 3x increase over the preceding 9x build). Ctrl+wheel zoom and horizontal scrolling are unchanged. Office previews
retain their previous scrolling speed even though they reuse the PDF component.
Images, text, audio, video, navigation, controls, settings and window geometry
retain their existing behavior. No keyboard shortcuts or UI controls are added.

## Arch Linux / CachyOS

The Arch recipe is 2.0.1-1 and downloads this release's source archive with a
pinned SHA-256. LibreOffice remains optional at runtime and is needed for the
complete Office conversion tests when building from source.

The source archive contains application sources, data, tests and documentation.
Downstream Arch packaging is maintained separately in the repository's
`packaging/arch` directory; it is excluded from the source asset so its checksum
can be pinned without a self-referential archive.

Binaries built on CachyOS may require x86-64-v3 startup objects and a newer Qt
minor than another Arch installation. Any supplied host-built binary must be
used only with the CPU/Qt requirements stated in the validation report. Rebuild
from source in a standard Arch environment for other supported systems.

The existing v2.0.0 tag and GitHub Release are preserved.
