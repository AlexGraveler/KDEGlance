# Arch Linux / CachyOS package: KDEGlance 2.1.0-1

The PKGBUILD downloads the pinned official v2.1.0 source archive, verifies SHA-256,
builds Qt6/KF6 and Dolphin integration, and runs check() without --nocheck.
Keep PKGBUILD, .SRCINFO and kdeglance.install together. No files go into $HOME.

## Without LibreOffice

```bash
git clone https://github.com/AlexGraveler/KDEGlance.git
cd KDEGlance/packaging/arch
makepkg -si
```

## With LibreOffice

```bash
sudo pacman -S libreoffice-fresh
git clone https://github.com/AlexGraveler/KDEGlance.git
cd KDEGlance/packaging/arch
makepkg -si
```

LibreOffice is only needed for supported Office previews. It is an optdepends
entry, absent from depends/makedepends/checkdepends. Office-specific tests skip
when it is unavailable; text/PDF/media and the remaining tests still run.
The 2.1.0 source includes this behavior; no local backport patch is needed.

The supplied binary targets CachyOS x86-64-v3 and Qt >= 6.12.0. Prefer a local
source build for other CPUs/Qt versions. Restart Dolphin after upgrading.

For future releases update pkgver, reset pkgrel=1, run make-source-tarball.sh to
export allow-listed source and update its SHA-256, then run
`makepkg --printsrcinfo > .SRCINFO`. The helper does not publish anything.
Review the source diff and build/test the package before publication.
AUR publication remains separate; no AUR upload is performed by these tools.
