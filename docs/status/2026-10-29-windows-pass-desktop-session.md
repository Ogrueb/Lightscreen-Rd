# 2026-10-29: Windows pass by a Claude desktop session, and its fixes

A Claude session in the Claude desktop app ran the [Claude] checks of the owner's test plan on the owner's PC (computer use, 3840x2160 display, scaled), with the owner running R5 and R7. Results are ticked and commented in the plan.

- **Passed:**
  - R2 (active window from the tray, via F9), R3, R5, R6, R7, R8, R9.
  - D1: build, exe properties, runs.
  - D2: draft, zip, runs, and clean-up (the draft was deleted with the owner's OK).
- **Failed, fixed here:**
  - **R1, window picker:** it picked the window behind at scaling above 100%. Now **bug 42**: the picker used Qt's device-independent mouse position with `WindowFromPoint`. New test `tst_Os::windowPickerPicksWindowUnderCursor` (Windows).
  - **D1, tests:** two `tst_os` checks failed on the scaled display. Now **bug 43**, a test bug: the offscreen test process was DPI unaware. `tst_Os::initTestCase` makes it per-monitor aware, as the app is.
- **Cosmetic, fixed:** with no optimizer found, "No optimizer found next to lightscreen.exe" was cut off at the dialog's default width. The tool box beside it is now hidden in that case, since there is nothing to choose.
- **Partial or not run:**
  - R1 step 1 used Notepad instead of Explorer, and passed.
  - R1 corners were skipped.
  - B1 migration does not apply (no pomf URL in the owner's settings).
  - B1's history-menu step: the first half passed.
  - B1 uploads were not run, as agreed.
- **Re-checks:** bug 42 with a real mouse at the owner's scaling (in the two window-capture checks), and `ctest` at that scaling for bug 43.
- **2026-10-29, owner's re-checks** on the `master` CI build at `dd6e58a`:
  - R1 window picker with a real mouse, Explorer partly off screen, and Windows 11 corners passed. The first try ran a build from before the fix and failed; the D1 build in `LS-Rd-test\src` predates PR #59.
  - R2 Print Screen from the tray passed.
  - R4 exact area sizes passed. The area dialog's size label shows the typed (device-independent) size, by design.
  - C1 passed. Lowering the scale while the app runs leaves a strip at the window's edge: **bug 44**, a known minor issue (owner).
- **QA files:** the passed checks are removed. Left: autostart (`2026-10-20-qt6-flip.md`), `ctest` at the owner's scaling (`2026-10-21-cmake-build.md`), the short two-monitor check (`2026-10-22-mixed-dpi-capture.md`, which replaces `2026-10-15-qscreen-capture-and-placement.md`), B1 (`2026-10-21-custom-uploader.md`, plus upload progress in `2026-10-16-taskbar-without-winextras.md`) and the release (`2026-10-22-release-v3.md`).
- **Two-monitor check** (short version, owner): whole screen, area across the join, a window on the second monitor and the picker's placement passed. A typed size used the main monitor's scaling on both monitors: **bug 45**, fixed (`CaptureGeometry::typedSizeToDevice`, two new tests). The check file holds the re-check.
- **B1, custom uploader with uguu.se** (owner): the test upload, the real upload, the taskbar's upload progress and the history menu passed. Cancel could not be tried, since the upload finished too quickly; `tst_Uploader::cancel` covers it. Migration does not apply. Both check files are removed.
- **Bug 45 follow-up** (owner, main monitor at 250%, second at 125%): the size tag read `50x50` for a typed `100x100`, and the typed text showed on the main monitor. Both fixed: the tag uses the selection's monitor's scaling, and the typed text is drawn at the selection. The check file holds the re-check.
- **SmartScreen** warned on the downloaded build. That is expected for an unsigned download, and the release notes say so.
- **Final checks** (owner, 2026-10-29): the bug 45 follow-up re-check on two monitors passed, R10 autostart passed, and D1 `ctest` passed all 13 suites at the owner's scaling after pulling `master` (bug 43 confirmed). Every check before the release has passed. Only `docs/windows-qa/2026-10-22-release-v3.md` is left.
