# KDEGlance 2.0.4

Maintenance release with fixes validated on Fedora; no new features.

- **Image zoom / KWin:** renew stale geometry waits after image navigation so
  wheel zoom continues working when the preview title changes.
- **Video crash prevention:** defer decoder-error cleanup until the Qt Multimedia
  callback returns, preventing the playback engine from being destroyed on its own
  stack. Ignore stale errors after navigation or closing. Normal video playback
  remains enabled.
- Include regression tests for both lifecycle fixes.

## Downloads

Use the existing, separately built x86_64 RPM for your Fedora version:

- Fedora 44: `kdeglance-2.0.4-1.fc44.x86_64.rpm`
- Fedora 45: `kdeglance-2.0.4-1.fc45.x86_64.rpm`

The RPMs are unchanged from the tested builds and are unsigned. Verify their
SHA-256 checksums with the accompanying `SHA256SUMS` before installation.
See [Fedora package verification](packaging/fedora/RELEASE-2.0.4.md) for hashes
and source provenance.

**Arch/CachyOS:** this update includes fixes validated on Fedora. The previously
published **2.0.3 Arch package remains unchanged**, as does its build configuration.
No new Arch package is supplied with 2.0.4.

## Validation

The release owner completed manual acceptance, including image scaling and video
playback. Existing build results: Fedora 45 CTest **16/16**; Fedora 44 **15/16**,
with a known RLE TGA format-test failure. The Fedora 44 RPM was packaged with
`--nocheck` after that recorded test run; it is not presented as passing all tests.
The video error path was tested against the old crashing binary and the fixed
binary with a controlled decoder initialization failure. Fedora 45 container and
offscreen checks do not certify a full Fedora 45 KDE Wayland/Live session.

The tagged application code, data, build metadata and tests match the source
archive embedded in both Fedora SRPMs. Release documentation was finalized after
those builds; embedded RPM documentation still describes the unpublished candidate.
No RPM rebuild was needed. Previous releases and their attachments are preserved.

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
