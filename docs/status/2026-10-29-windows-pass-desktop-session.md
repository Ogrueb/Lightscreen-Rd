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
