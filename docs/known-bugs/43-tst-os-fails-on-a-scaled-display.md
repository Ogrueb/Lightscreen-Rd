# 43. `tst_os` failed on a scaled display (test bug)

- **Found:** in the 2026-10-29 Windows pass (D1, `ctest` on the owner's PC, scaled display). 12 of 13 suites passed. `tst_Os::pixmapFromHICON` got a 40-pixel icon against `SM_CXICON`'s 32. `tst_Os::grabWindowPartlyOffScreen` got a 400x240 capture against a 964x732 frame.
- **Cause:** the tests run on Qt's offscreen platform, which, unlike the windows platform the app uses, leaves the process DPI unaware. Windows then reports some sizes in scaled pixels and others in physical pixels, and the tests compared the two. CI runs at 100%, where they agree. The app itself is per-monitor aware; D1's "Runs" step saved a full 3840x2160 screenshot.
- **Fix:** `tst_Os::initTestCase` makes the process per-monitor DPI aware (`SetProcessDpiAwarenessContext`), as the app is.
- **Platform:** Windows, tests only. **Status:** fixed in the test; all 13 suites passed at the owner's scaling on 2026-10-29.
