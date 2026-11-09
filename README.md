## Lightscreen Rd
[![CI](https://github.com/Ogrueb/Lightscreen-Rd/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/Ogrueb/Lightscreen-Rd/actions/workflows/ci.yml)

A screenshot tool for Windows and Linux.

Lightscreen Rd ("Redeveloped") is a maintained fork of [Lightscreen](https://github.com/ckaiser/Lightscreen) by Christian Kaiser, which has not been updated in several years. The original code is kept as the base and rebuilt on current frameworks, so the app looks and works as it did. Please report problems with Lightscreen Rd here, not to the original project.

Settings and screenshot history from Lightscreen 2.5 carry over: Lightscreen Rd reads them from the same place.

### What changed from Lightscreen 2.5

- **Qt 6.8 and Windows 10 or later** (64-bit), built with CMake. Linux (X11) is kept and tested in CI.
- **Configurable uploader** in place of Imgur and pomf: upload URL, form field, optional headers, and where the link is in the reply (Options > Upload), with a "Test upload" button. A pomf URL from older settings is carried over.
- **PNG optimization queue**, with oxipng or OptiPNG chosen in the options; one file at a time, the original replaced only on success, and pending files resumed after a restart. The Windows package includes both tools.
- **Capture on mixed-DPI setups**: each screen is grabbed at its own scale, and area selection maps to exact pixels at fractional scaling.
- **HDR monitors**: with HDR on, captures match what the screen shows; browsers used to come out several times too bright. The "HDR capture" option (Options tab) switches back to the plain capture, and `lightscreen --hdr-report` writes a diagnostic file to the desktop for bug reports.
- **Update check** against this fork's GitHub Releases; no calls to the original author's server.
- **Many bugs fixed**: see [docs/known-bugs.md](docs/known-bugs.md).
- CI on Windows and Linux for every change, with a test suite (QtTest) and a release workflow.

### Building

See [docs/build.md](docs/build.md) for Windows (MSVC) and Linux, and for running the tests. Every CI run on Windows also uploads a ready-to-run `lightscreen-win64` zip; releases are published as `lightscreen-rd-<version>-win64.zip`.

### License

GPL v2 (see [LICENSE](LICENSE)). `dialogs/areadialog.*` is based on KDE's KSnapshot and keeps its LGPL notice.
