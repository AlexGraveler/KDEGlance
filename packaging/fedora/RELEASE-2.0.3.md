# Fedora 44: KDEGlance 2.0.3-1 build status

Prepared on CachyOS, 2026-11-10. No Fedora RPM 2.0.3 or SRPM 2.0.3 is claimed
as built or tested here: rpmbuild/mock/container runtimes are unavailable.
Do not rename the previous 2.0.2-3.fc44 binary.

The imported 2.0.2-3 source RPM matches all 75 production src/data files in the
transferred Fedora tree. All 63 matching debugsource compilation inputs and
the binary RPM's KWin placement script match too. This is source correspondence,
not execution of a Fedora binary on CachyOS.

Build on a Fedora 44 x86_64 machine:

```bash
sudo dnf install rpm-build rpmdevtools dnf5-plugins mock
sudo dnf builddep packaging/fedora/kdeglance.spec
python3 packaging/fedora/make-source-tarball.py
rpmbuild -ba --define "_topdir $PWD/packaging/fedora/rpmbuild" packaging/fedora/kdeglance.spec
```

For a clean root, build the SRPM with mock:

```bash
mock -r fedora-44-x86_64 --rebuild packaging/fedora/rpmbuild/SRPMS/kdeglance-2.0.3-1.fc44.src.rpm
```

Keep `%check` enabled. It runs all CTest sets, desktop-file and AppStream
validation. Record the resulting RPM/SRPM SHA-256, Qt build version and test
logs. Install with `sudo dnf install ./kdeglance-2.0.3-1.fc44.x86_64.rpm`,
verify `rpm -V kdeglance`, restart Dolphin and repeat
`python3 packaging/fedora/test-installed.py` with the development adapter built.
Use the no-record desktop geometry tests; do not regenerate project screenshots.
Only after those checks may the new RPM/SRPM be added to a release candidate.

`make-source-tarball.py` reuses the Arch exporter, so both distributions consume
byte-identical deterministic production source archives. Arch PKGBUILD lives
outside that archive to avoid embedding its own checksum. LibreOffice is
a test build dependency and remains optional at runtime.

Historical Fedora checks: [validation](VALIDATION.md),
[zoom](ZOOM-VALIDATION.md), [geometry](GEOMETRY-TRANSACTIONS.md).
