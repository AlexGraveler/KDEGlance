# Contributing to KDEGlance

The current application is C++20 with Qt6 and KDE Frameworks 6. Build instructions
and dependencies are in [docs/INSTALL.md](docs/INSTALL.md); test and desktop
acceptance instructions are in [docs/TESTING.md](docs/TESTING.md).

Preserve the accepted module behavior. Keep changes focused, include a concrete
reproduction for bugs, and use regression tests that exercise behavior rather
than duplicating implementation. Never present offscreen tests or injected Qt
input as physical Wayland pointer acceptance.

Do not commit generated packages, build caches, personal settings, screenshots of
private documents, credentials or local desktop logs. Synthetic fixtures and
intentional UI screenshots are kept in the repository. Use the source-archive
helper for a clean release export. Changes to versions, licensing and published
releases must be intentional and documented.
