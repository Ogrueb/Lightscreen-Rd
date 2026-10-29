# Changelog

Changes in Lightscreen Rd, the maintained fork of Lightscreen by Christian Kaiser. Versions before 3.0 are upstream Lightscreen; their history is in the [original repository](https://github.com/ckaiser/Lightscreen).

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Bug numbers refer to `docs/known-bugs/`.

## [3.0.0] - unreleased

First release of the fork, based on upstream Lightscreen 2.5. Settings and screenshot history from 2.5 are read from the same place and carry over unchanged.

### Changed

- **Name:** Lightscreen Rd ("Redeveloped"). Window titles, the tray tooltip and the About tab say so; the executable is still `lightscreen.exe`, and the start-at-login entry is still "Lightscreen", so an upgrade reuses it.
- **Platform:** Qt 6.8 (was Qt 5), Windows 10 or later, 64-bit only. Linux (X11) is still built and tested in CI. Windows XP to 8.1 and 32-bit builds are no longer supported.
- **Uploader:** Imgur and pomf are replaced by one uploader you configure in Options > Upload: upload URL, form field, optional headers, and where the link (and an optional delete link) is in the server's JSON reply. "Test upload" checks the setup. A pomf URL from 2.5 settings is carried over. Imgur is gone because it relied on credentials built into the app.
- **Update check:** reads this fork's GitHub Releases instead of `lightscreen.com.ar`, and says so when the check fails instead of reporting "up to date" (bug 6).
- **History dialog:** "Delete from imgur.com" is now "Open delete link", for any upload that returned one.
- **Defaults:** every setting has one default (the options dialog's). A settings file with keys missing now behaves as a fresh install would: save to `Documents\Screenshots` with the `screenshot.` prefix, PNG format, tray icon on. Existing complete settings are unaffected.
- **Sound:** played without QtMultimedia; the error sound is the system default sound.
- **Dark mode:** the main window follows the Windows light or dark theme (Qt 6), with light icons on dark buttons.
- **Display scaling:** the app's windows follow the exact Windows scale (125%, 150%, 175%) instead of being rounded to 100% or 200%.

### Added

- **PNG optimizer choice:** oxipng or OptiPNG, next to "Optimize PNG screenshots" in the options. If the chosen tool is missing, the other one runs. The Windows package includes both (oxipng 10.2.1, OptiPNG 7.9.1), with their licences in `licenses\`.
- **Optimization queue:** one optimizer process at a time, in order; the original is replaced only when the tool succeeds; files still waiting when the app stops are optimized after the next start. The tray tooltip shows "N optimizing" (bug 10).
- **Mixed-DPI capture:** each monitor is captured at its own scaling and the monitors are joined at their real pixel sizes, with the cursor placed correctly (bug 4). Area selection maps to exact pixels at fractional scaling and across monitors (bugs 12, 23).

### Fixed

- Crash when an upload fails with the tray icon off (bug 27).
- A second screenshot request during the delay took the first one's mode and left a timer running forever (bug 28).
- Collision naming could pick an existing file and overwrite it, or count unrelated files (bugs 15, 17).
- A failed save reported twice and still wrote a history row; a temporary `.screenshot.` file was left behind with file and clipboard both on (bugs 16, 18).
- Uploading a screenshot that was already in the history added a second row instead of updating it (bug 13).
- Uploads from the history dialog ignored the chosen service; an unknown service name crashed; cancelling an upload reported an error (bugs 14, 21, 22).
- WEBP, and any other value the dialog could not show, was lost when the options were saved (bug 19); an unknown format setting produced a file with no extension (bug 2). The Windows package now includes Qt's WEBP plugin, so WEBP screenshots are written (bug 39).
- An upper-case X in a typed area size ("100X50") was ignored (bug 24).
- The date format box in the Naming dialog took `?`, `:` and other characters a file name cannot hold (bug 40), and the Directory box suggested no folders (bug 41).
- A hotkey could not be set from the keyboard (bug 26).
- Window capture left a border strip or cut the window's edge on Windows 10/11, also on a secondary monitor (bug 5); the window picker showed no icons on 64-bit Windows (bug 1).
- `lightscreen --quit` with no instance running started a hidden one (bug 29), and with "Closing hides the main window" on and no tray icon it waited on a dialog.
- "Run at startup" did nothing on Linux when `~/.config/autostart` did not exist (bug 25).
- SSL errors were ignored on old Windows versions (bug 7).

### Removed

- Imgur upload and the built-in pomf host list.
- Every call to `lightscreen.com.ar`.
- Windows XP to 8.1 code paths, the 32-bit and MinGW builds, and the qmake build.

### Known issues

- Browsers on an HDR monitor can be captured brighter than they appear (bug 9).
- The window picker shows no icon for Windows Store apps such as Settings (bug 36).
- With "Sound cue" and "Tray icon Popup" both on, Windows plays its notification sound with the shutter. Turn the pop-up off, or turn off the sound for Lightscreen Rd's notifications in Windows' notification settings (bug 37).
- On Windows 11, Print Screen opens Windows' Snipping Tool while "Use the Print screen key to open screen capture" is on (Settings > Accessibility > Keyboard; on by default). Turn it off, or give Lightscreen Rd another hotkey.
- PNG screenshots always use compression quality 80, whatever the quality setting (bug 20).
- Lowering the display scaling while the app runs can leave a strip at the main window's right edge until the window is resized (bug 44).
- Wayland and macOS are not supported.

### For developers

- CMake build with presets (Windows MSVC, Linux); no submodules (SingleApplication and UGlobalHotkey are vendored).
- GitHub Actions CI on Windows and Linux for every pull request: build, 13 QtTest suites, Linux smoke tests, and a dependency check between `tools/` and the UI code.
- Release workflow: a tag `v<version>` builds the Windows zip and opens a draft GitHub Release.
- Decisions, known bugs, status and manual Windows checks are recorded under `docs/`.

[3.0.0]: https://github.com/Ogrueb/Lightscreen-Rd/releases/tag/v3.0.0
