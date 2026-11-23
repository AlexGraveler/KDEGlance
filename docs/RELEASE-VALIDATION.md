# KDEGlance 2.1.0 release validation

Date: 2026-11-23. Release base: v2.0.4 (36ac3bf), a normal descendant of
v2.0.3/v2.0.2/v2.0.1/v2.0.0. Accepted local features were merged using v2.0.2
as the three-way base. No prior release/tag is moved or replaced.

The project owner reports all 18 manual acceptance checks passed on the accepted
pre-release build, and 18/18 automated suites previously passed. Final package
build/test output, per-platform checks, artifact hashes and limitations are
recorded in the release's attached VALIDATION.md; source presence alone is not
reported as GUI acceptance. Existing historical validation remains in Git history
and the distribution-specific reports.

The release retains coordinated initial geometry, fractional-scale/cursor math,
stale KWin long-poll renewal and deferred decoder-error cleanup from 2.0.3/2.0.4.
New behavior has tests for syntax/theme changes, Markdown persistence, shared
filenames, controls hiding, media zoom and independent repeat/reset.

The CachyOS binary requires x86-64-v3, Qt >= 6.12.0 and compatible rolling
libraries. Other systems should build the source or use a matching RPM only
when that package appears in the release assets and its validation is listed.
LibreOffice is optional; only converter-specific tests skip when it is absent.
No global KWin shortcuts, KDE settings or MIME associations are changed.
