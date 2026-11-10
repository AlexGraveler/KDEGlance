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

## Validation and downloads

Both full CTest runs passed **16/16**. The focused wheel test passed all 18 data
scenarios; six selected scenarios also passed in native Wayland widgets using
synthetic Qt input. No physical mouse/Dolphin acceptance is claimed.
See VALIDATION.md for the exact checks and limits; verify files with SHA256SUMS.

The host-built binary package is **2.0.2-1** and requires **x86-64-v3 and
Qt >= 6.12.0**, plus compatible current system libraries. Build from source in
a suitable standard Arch environment for other supported CPU/Qt combinations.

This is a new release. Existing 2.0.1 and 2.0.0 releases/tags/assets are unchanged.


## Fedora 44

KDEGlance 2.0.3 adds Fedora 44 packaging to the shared Arch/CachyOS codebase,
including the common Wayland image/video geometry and Zoom to Cursor fixes.
The existing screenshots and Arch/CachyOS instructions above are preserved.
The version numbers and validation counts above describe v2.0.2; the current
2.0.3 source/Arch assets and checks are listed in SHA256SUMS and VALIDATION.md.

A Fedora 44 RPM/SRPM 2.0.3 has not been built in this CachyOS environment and
is not supplied as a tested binary. Build it on Fedora 44 from the v2.0.3
repository using the included recipe:

```bash
sudo dnf install rpm-build rpmdevtools dnf5-plugins
sudo dnf builddep packaging/fedora/kdeglance.spec
python3 packaging/fedora/make-source-tarball.py
rpmbuild -ba --define "_topdir $PWD/packaging/fedora/rpmbuild" packaging/fedora/kdeglance.spec
sudo dnf install ./packaging/fedora/rpmbuild/RPMS/$(uname -m)/kdeglance-2.0.3-1.fc44.*.rpm
```

LibreOffice remains optional at runtime; install `libreoffice-writer` separately
for Office previews. Reopen Dolphin after installation. No global shortcuts,
MIME defaults, SELinux policy or KWin settings need changing. The original
Fedora 2.0.2-3.fc44 RPM is retained only as a previous-version reference, not
renamed or presented as 2.0.3.
