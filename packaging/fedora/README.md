# Fedora KDE Plasma packaging

Build locally from the working tree; no GitHub release or upload is performed.
Requires Fedora with Qt >= 6.8, KDE Frameworks 6 and Dolphin. The source minimum
is independent of the binary RPM's Qt minimum, which records the Qt used to build
the Dolphin plugins. Fedora uses `lib64/qt6/plugins` on x86_64, while the Arch
recipe continues to use `lib/qt6/plugins`.

## Build and install

From the repository root:

```bash
sudo dnf install rpm-build rpmdevtools dnf5-plugins
sudo dnf builddep packaging/fedora/kdeglance.spec
python3 packaging/fedora/make-source-tarball.py
rpmbuild -ba --define "_topdir $PWD/packaging/fedora/rpmbuild" packaging/fedora/kdeglance.spec
sudo dnf install ./packaging/fedora/rpmbuild/RPMS/$(uname -m)/kdeglance-2.0.3-1.fc44.*.rpm
```

The archive includes current source edits without modifying the Arch recipe or
its release checksum. Build products stay in the ignored `rpmbuild/` directory.
The RPM is unsigned and intended for local testing. Use a clean Fedora `mock`
build when preparing distributable binaries; a host build is not a clean-root
certification. Do not use `--nodeps` to install the package.

`%check` runs CTest, desktop-file validation and offline AppStream validation.
LibreOffice Writer, FFmpeg, ImageMagick, Node.js, qpdf and Python are test build
dependencies. LibreOffice is deliberately absent from runtime Requires and weak
dependencies: installing the binary RPM does not pull it in. To enable Office
previews separately, use `sudo dnf install libreoffice-writer`.

Restart Dolphin after installing/upgrading. The on-demand user service has no
`[Install]` section and must not be enabled as a boot service. RPM systemd user
macros handle unit reload, upgrade and removal. Do not change global shortcuts,
MIME associations or SELinux policy to install KDEGlance.

## Verification

```bash
rpm -qp --requires packaging/fedora/rpmbuild/RPMS/$(uname -m)/kdeglance-2.0.3-1.fc44.*.rpm
rpm -ql kdeglance
rpm -V kdeglance
kdeglance --version
kdeglance --status
systemctl --user status kdeglance.service
```

`--status` activates the service through the session bus. On a real Plasma
session, select a synthetic image in a newly started Dolphin and press Space;
check arrow navigation, selection synchronization and Escape. Test the context
menu too. The CTest Dolphin integration uses mocks and does not replace this
desktop check. See [the manual checklist](../../docs/MANUAL-ACCEPTANCE.md).

Repeat the automated desktop smoke check in a Plasma session with no preview
open. It opens and closes a dedicated Dolphin window with synthetic images;
the adapter is a development-only target and is never shipped in the RPM:

```bash
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_DESKTOP_TEST_ADAPTER=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure -j2
python3 packaging/fedora/test-installed.py
```

To check optional LibreOffice without uninstalling it, install `bubblewrap` and
run `bash packaging/fedora/test-without-libreoffice.sh build` after CTest has
generated its media samples. Only the converter inside the test namespace is
hidden; the host installation is unchanged.

```bash
sudo dnf remove kdeglance
rpm -q kdeglance
```

After removal the last query should report that the package is not installed.
Restart Dolphin to unload its plugins. User settings are retained. Build tools
and test dependencies installed explicitly are not removed automatically.

See [the Fedora validation report](VALIDATION.md) for the actual tested versions,
results, codec coverage limits and remaining packaging warnings.
The local `2.0.2-2` geometry/zoom update is covered in
[the zoom validation report](ZOOM-VALIDATION.md).
