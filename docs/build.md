# Building Lightscreen

Since M09 (2026-10-20) the app builds with **Qt 6** (6.2 or newer) and C++17 only; Qt 5 is no longer supported. Since M11 (2026-10-21) the build is **CMake** (3.21 or newer) with the presets in `CMakePresets.json`; the qmake files are gone. One configure builds the app and the tests. CI and the Windows package use Qt 6.8 (the current LTS) from `aqtinstall` on both platforms. Both builds run in GitHub Actions on every pull request and every push to `master` (see "Continuous integration" below). Upstream had no tests; the test harness added in M02 is described under [Tests](#tests), and CI runs it on both platforms.

## Windows (verified)

### Toolchain

| Component | Version | Installed with |
|---|---|---|
| Visual Studio 2022 Build Tools, "Desktop development with C++" workload | 17.14 (MSVC 19.44.35229, x64) | `winget` |
| Qt | 6.8.3 (LTS), `win64_msvc2022_64` kit, the version pinned in `.github/workflows/ci.yml` | `aqtinstall` |
| CMake and Ninja | the ones bundled with the Build Tools (CMake 3.31 with 17.14) | the "C++ CMake tools for Windows" component, part of the workload's recommended set |
| Python (only to run aqtinstall) | 3.12 | already present |

All required Qt modules (core, gui, widgets, network, sql with the SQLite driver, concurrent, testlib) are in the base `qtbase` package. The one add-on, `qtimageformats`, holds the WEBP plugin: without it the app builds and runs, but a WEBP screenshot writes no file, and on Windows `tst_screenshot` fails. QtWinExtras and QtMultimedia are no longer used (M06, M07).

### One-time setup

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
python -m pip install --user aqtinstall
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtimageformats --outputdir C:\Qt
```

The Build Tools install needs an admin prompt (about 6 GB). `--includeRecommended` brings in CMake and Ninja; `vcvars64.bat` puts both on `PATH`. If `cmake --version` fails in the prompt below, add the component with the Visual Studio Installer (Modify > Individual components > "C++ CMake tools for Windows"), or install them separately with `winget install Kitware.CMake Ninja-build.Ninja`. Qt goes into `C:\Qt` (about 1.5 GB, no admin). Run the `aqt` command from outside the checkout: it writes `aqtinstall.log` to the current directory.

### Build

From a plain `cmd` prompt in the checkout:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;%PATH%
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

CMake finds Qt through `PATH` (it looks one level above each `bin` directory); to use another Qt, pass `-DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64` to the first command. The preset uses Ninja and `cl`, which is why it must run from the `vcvars64` prompt.

Run every command in one `cmd` window (or open "x64 Native Tools Command Prompt for VS 2022", which runs `vcvars64.bat` for you). PowerShell does not keep what `vcvars64.bat` sets, so `cmake` is "not recognized" there, and a new window starts without `vcvars64`, CMake and Qt on `PATH`. If every test fails at once with exit code `0xc0000135`, Qt's `bin` is missing from `PATH` in that window. Run the tests through `ctest`: a test `.exe` started directly uses the real windows platform instead of `offscreen`, opens real windows and can hang (found in the 2026-10-29 Windows pass).

Output: `build\windows-msvc-release\lightscreen.exe`, with the tests under `build\windows-msvc-release\tests\`. `build/` is git-ignored. A fresh configure is needed once after switching from a qmake checkout: delete the old `build\` folder first.

### Run / package

The exe needs the Qt DLLs next to it:

```bat
mkdir deploy && copy build\windows-msvc-release\lightscreen.exe deploy\
windeployqt --release --no-translations deploy\lightscreen.exe
deploy\lightscreen.exe
```

## Linux (verified in CI)

CI uses Qt 6.8.3 from `aqtinstall` (`linux_gcc_64`) on Ubuntu 24.04, plus the X11/xcb and OpenGL headers and the xcb runtime libraries from apt (the exact list is in the `linux` job of `.github/workflows/ci.yml`). For a local build, Ubuntu's own Qt 6 is enough: 24.04 ships 6.4, older than CI's but new enough for every API the code uses (checked in M09: the app and the tests build and the tests pass on it). CI's version is the reference.

```sh
sudo apt-get install cmake ninja-build qt6-base-dev libqt6sql6-sqlite libgl-dev \
  libx11-dev libxcb-keysyms1-dev xvfb
cmake --preset linux-release      # or linux-debug
cmake --build --preset linux-release
```

Output: `build/linux-release/lightscreen` (`build/linux-debug/` for the debug preset), with the tests under `tests/` next to it. To use aqt's Qt instead of Ubuntu's, add `-DCMAKE_PREFIX_PATH=<qt>/6.8.3/gcc_64` to the configure command. Ubuntu 24.04's CMake is 3.28, newer than the 3.21 minimum.

The X display and xcb connection come from `QNativeInterface::QX11Application` in QtGui (M08), so neither QtX11Extras nor private Qt headers are needed. No private Qt headers are needed any more (until M08, UGlobalHotkey included `qpa/qplatformnativeinterface.h`). `xdotool` is only for the hotkey smoke in CI. The app needs an X display (real or `xvfb-run`); with `QT_QPA_PLATFORM=offscreen` it segfaults at startup.

## Continuous integration

`.github/workflows/ci.yml` runs on every pull request and every push to `master`. A **docs** job checks the docs layout (`.github/scripts/check-docs.sh`) and the include graph (`tests/check-includes.sh`), and decides whether the two build jobs run: on a pull request whose changed files are all under `docs/` or end in `.md`, the linux and windows jobs are skipped (they show as skipped, which counts as passing); every other pull request and every push to `master` runs them. The build jobs:

- **linux** (`ubuntu-latest`): installs the apt packages and Qt 6.8 `linux_gcc_64` from `jurplel/install-qt-action` (cached), configures and builds with the `linux-release` preset (app and tests), then runs the *Linux smoke* from `build/linux-release`. It writes a `config.ini` next to the binary (portable mode, so no user settings are touched) with `file/format`, `file/enabled` and `file/target` set so the first-run options dialog is skipped, `options/tray=false` so no system tray is needed, and `options/disableUpdater=true` so the run makes no network calls. Under `xvfb-run -a` it starts `./lightscreen`, waits two seconds, runs `./lightscreen --quit` (SingleApplication forwards it to the running instance), and fails unless the first process exits with status 0 within ten seconds. It then runs `./lightscreen --quit` again with no instance running, which must exit with status 0 within ten seconds (bug 29). A second step, the *hotkey smoke*, starts the app with the whole-screen action on Print Screen (`actions/screen/enabled=true`, `actions/screen/hotkey=Print`), sends the key with `xdotool key Print`, and fails unless a PNG appears in the target folder within ten seconds; this covers the UGlobalHotkeys X key grab end to end.
- **windows** (`windows-latest`): Qt 6.8 `win64_msvc2022_64` with `qtimageformats` from `jurplel/install-qt-action` (cached), MSVC x64 from `ilammy/msvc-dev-cmd`, the `windows-msvc-release` preset (Ninja and `cl` from the runner's Visual Studio), then `windeployqt --release --no-translations` on a copy of `lightscreen.exe`, failing unless it deployed `imageformats\qwebp.dll` (the release workflow checks the same). `.github/scripts/fetch-optimizers.ps1` then adds `oxipng.exe` (10.2.1) and `optipng.exe` (7.9.1) from their upstream release archives, each checked against the SHA-256 recorded in the script, with their licence files under `licenses\<tool>\`; the release workflow runs the same script. The *Windows smoke* (`.github/scripts/windows-smoke.ps1`) then runs a copy of the deploy folder on the runner's desktop (GitHub's Windows runners have a real signed-in desktop session, one 1024x768 screen; checked by a probe on 2026-10-27). It writes a portable `config.ini` with the whole-screen action on F9 and the tray icon on, starts `lightscreen.exe`, presses F9 from outside the app until a PNG appears (20 seconds at most), checks that the PNG has the screen's size, and fails unless `lightscreen --quit` ends the app within ten seconds. This covers hotkeys from the tray end to end (bug 33). It uses F9 because a Print Screen sent with `keybd_event` never reached the app on the runner. The deploy folder is uploaded as the `lightscreen-win64` artifact on the run's page; unzip it and run `lightscreen.exe`.
- Both jobs then run `ctest --preset <preset>` (see [Tests](#tests)): Linux under `xvfb-run -a`, Windows with `QT_QPA_PLATFORM=offscreen` (set by the Windows test preset). A failing test fails the job, and ctest prints the output of each failing test.

To run the smoke locally after a Linux build, copy the script from the "Smoke test" step of the workflow into `build/linux-release/smoke.sh` and run `xvfb-run -a bash smoke.sh` from `build/linux-release/` (the same for the "Hotkey smoke" step, which needs `xdotool`). If a previous run was killed, a stray `lightscreen` process will make the next run forward its arguments and exit; kill it first.

## Releases

`.github/workflows/release.yml` (M15) builds the Windows package the same way as CI (Qt 6.8.3, MSVC, the `windows-msvc-release` preset, the tests, `windeployqt --release --no-translations`) and zips it as `lightscreen-rd-<version>-win64.zip`, with one top-level folder of the same name inside. It then unpacks the zip, fails unless it holds exactly that folder with `lightscreen.exe`, the Qt DLLs and the `platforms`, `imageformats\qwebp.dll`, `sqldrivers` and `tls` plugins, `oxipng.exe`, `optipng.exe` and both `licenses` folders, and runs the Windows smoke on the unpacked app. The version is the one in `project(... VERSION ...)` in the root `CMakeLists.txt`. The workflow runs in three ways:

- **A `v*` tag** (for example `v3.0.0`): the run fails unless the tag names the version in `CMakeLists.txt` (`v2.5` and `v2.5.0` both match `2.5`; the update check compares the two), then creates a *draft* GitHub Release for that tag with the zip attached. Write the release notes on the draft and publish it by hand; only a published release is seen by the update check. Releases are unsigned. The text for the v3.0.0 draft is in `docs/release-notes/v3.0.0.md`; see `CHANGELOG.md` for what each version changed.
- **"Run workflow"** on the Actions tab (`workflow_dispatch`): a dry run. Same build, then a draft *pre-release* named "Dry run N" under the tag `dry-run-N`, attached to the commit that was built. A draft does not create its tag, and a pre-release is never "latest", but delete the draft after looking at it rather than publishing it.
- **A pull request that changes `release.yml`**: build and zip only (the zip is on the run's page as an artifact), no release.

There is no Linux package: Linux users build from source.

## What broke and how it was fixed

Nothing needed fixing to build. Things noticed along the way, left unchanged on purpose:

- **Windows XP targeting flags and the MinGW branch** (`_ATL_XP_TARGETING`, `/D _USING_V110_SDK71`, `/SUBSYSTEM:...,5.01`, which caused the linker warning `LNK4010`, and an unmaintained MinGW branch) were removed in M03, when the minimum became Windows 10. Only MSVC is supported on Windows.
- **README badges** pointed at the original author's AppVeyor and Coverity projects. Replaced by the fork's CI badge in M01; M14 rewrote the README as a fork notice that points here for building.
- **Dead files.** `dialogs/aboutdialog.ui` (not in `FORMS`, never used) and the `OTHER_FILES += TODO.txt` line in the old `lightscreen.pro` (the file never existed) were removed in M14.
- **qmake to CMake (M11).** `lightscreen.pro`, `src.pri`, `version.pri`, the tests' `.pro`/`.pri` files and the vendored libraries' `.pri`/`.pro` files were replaced by `CMakeLists.txt` files in the same places. What qmake generated on Windows is now written out: `cmake/lightscreen.rc.in` holds the icon (`RC_ICONS`) and the version information (`VERSION`, `QMAKE_TARGET_*`), and the `/MANIFESTUAC:level='asInvoker'` link option stays.
- **Compiler warnings worth a look** (14 in total, the rest are `size_t` to `int` narrowing in UGlobalHotkey and `lightscreenwindow.cpp:113`):
  - `tools/windowpicker.cpp:168` (C4312): `GetClassLong(..., GCL_HICON)` returns a 32-bit value, which truncates the icon handle on 64-bit builds. `GetClassLongPtr` / `GCLP_HICON` is the 64-bit-safe call. Likely a real bug in the window picker's icon on x64.
  - `tools/screenshot.cpp:404` (C4715): `Screenshot::extension()` has no return for formats outside its `switch` (WEBP was removed from the UI but is still in the enum). Undefined behavior if it is ever hit.
- **Deprecations.** Since M09, `QT_DISABLE_DEPRECATED_BEFORE=0x060000` makes any API Qt deprecated before 6.0 a compile error. APIs deprecated later (`qAsConst`, the old `QMessageBox::question` overload, `QSqlDatabase::exec`) still compile with a warning; M10 cleans up the first.

## Tests

The tests live under `tests/`, one directory per test (`tests/tst_<name>/`), all listed in `tests/CMakeLists.txt` (one `lightscreen_add_test(<name>)` line each). The application's sources, everything but `main.cpp`, form the `OBJECT` library `lightscreen_core` in the root `CMakeLists.txt`; the app and every test link it, so the sources compile once and a file added to the app goes into that list only. SingleApplication and UGlobalHotkey are static libraries built from their own `CMakeLists.txt` under `tools/`. Each test executable is built into its own directory (`build/<preset>/tests/tst_<name>/`), as before: `tst_history` and `tst_optimizer` put files next to their executable. `tests/optimizer_stub` is built twice, as `oxipng` and as `optipng`, into `tst_optimizer/stub/oxipng/` and `tst_optimizer/stub/optipng/`. `tst_optimizer`'s `realTool` cases run the real tools when they are on `PATH` and skip otherwise (Linux CI installs both: optipng from apt, oxipng 10.2.1 with `cargo install`, cached).

`tst_smoke` checks that the harness builds and that the seams below can be installed. The Stage 4 safety-net tests sit next to it, one directory per module: `tst_settings`, `tst_screenshot`, `tst_history`, `tst_uploader`, `tst_hotkeywidget`, `tst_areadialog`, `tst_mainwindow` and `tst_os` (what each records: `docs/status.md`). Stage 6 adds `tst_updater` (M13: the GitHub Releases update check against a stub network). `tst_mainwindow` and the window tests in `tst_settings` skip on Windows and offscreen.

### Running the tests

The build above already built the tests. Linux, from the checkout:

```sh
xvfb-run -a ctest --preset linux-release
```

Windows, from the `cmd` prompt set up as in the Windows build section (the preset sets `QT_QPA_PLATFORM=offscreen`):

```bat
ctest --preset windows-msvc-release
```

ctest runs every test and prints the output of the failing ones. `ctest --preset <preset> -R tst_history` runs one; a test can also be run directly, e.g. `build/linux-release/tests/tst_smoke/tst_smoke` (Linux) or `build\windows-msvc-release\tests\tst_smoke\tst_smoke.exe` (Windows), with the usual QtTest options (`-v2`, a test function name). To configure without the tests, pass `-DBUILD_TESTING=OFF`.

Tests can run with `QT_QPA_PLATFORM=offscreen` except those that construct `LightscreenWindow`: it creates `UGlobalHotkeys`, which needs a real X display on Linux (see `docs/status.md`), so those need `xvfb-run`.

### Writing a test

Each test starts with `QStandardPaths::setTestModeEnabled(true)` and `QSettings::setDefaultFormat(QSettings::IniFormat)` so it never touches the developer's settings or data, works in a `QTemporaryDir`, and installs its own singletons through the seams:

- `ScreenshotManager(QSettings *settings, const QString &historyPath, QObject *parent)` builds a manager on the given settings (not owned) and history directory (with a trailing separator), then `ScreenshotManager::setInstance(&manager)` makes it the one `ScreenshotManager::instance()` returns.
- The manager owns its `Uploader` (since S02): `manager.uploader()->setNetwork(&nam)` makes every upload use `nam` (not owned), typically a `QNetworkAccessManager` subclass whose `createRequest` returns a canned reply. `Updater` takes the network manager as an argument, so a test passes its stub directly. The options dialog's "Test upload" (`UploaderOptionsWidget`, since S03) uses the uploader's network manager too, so the same stub catches it.
- A `Screenshot` built by the test (not by `ScreenshotManager::take`) reaches the history and the uploader only through its `saved` and `uploadRequested` signals: call `manager.connectScreenshot(&screenshot)` to wire them as `take` does.
- `Screenshot::extensionFor(Format)` and `Screenshot::uniqueName(nameWithoutExtension, extension)` expose the extension lookup and the " (n)" collision naming used by `Screenshot::save()`.

`setInstance` does not delete the previous instance; reset it to `nullptr` at the end of the test so a destroyed object is not left installed.

## Known flaky tests

None so far.

## Third-party libraries

SingleApplication and UGlobalHotkey used to be git submodules pointing at the original author's repos. They are now vendored (copied unchanged) under `tools/SingleApplication` and `tools/UGlobalHotkey`, so a plain clone builds with no `git submodule` step. Each directory has a `VENDORED.md` with its origin and pinned commit. The reasoning is in `docs/decisions.md`.

## Update check

The update check (`updater/updater.cpp`) asks `https://api.github.com/repos/<UPDATE_REPO>/releases/latest` and compares the release's `tag_name` (a leading `v` is dropped) with the app version (`project(... VERSION ...)` in the root `CMakeLists.txt`, compiled in as `APP_VERSION`). `UPDATE_REPO` (`owner/name`) is set once, as `LIGHTSCREEN_UPDATE_REPO` in the root `CMakeLists.txt`; the about tab's GitHub link and the "no release yet" message use it too, so the rename (M16) changes that one line. Drafts and pre-releases are not "latest" on GitHub, so they are never offered. Until the repository has a published release, "Check now" in the options shows "No release has been published yet".
