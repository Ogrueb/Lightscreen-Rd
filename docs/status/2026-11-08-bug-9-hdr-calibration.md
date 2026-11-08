# 2026-11-08: bug 9 follow-up, the 3.0.1 HDR capture corrected

The owner tested the 3.0.1 draft on the HDR monitor (Windows 11, SDR content brightness 400 nits, i.e. SDR white level 5.0). Area capture was still too bright, window capture was 2.5 times too dark (white 170), and a yellow border flashed on window capture. The draft release is held.

- **Diagnostics:** `HdrCapture::report()`, run with `lightscreen --hdr-report` (writes `lightscreen-hdr-report.txt` to the desktop) or `tst_hdrcapture report`, lists per screen and per Chromium, Explorer or Notepad window: the DXGI output, the SDR white level, failed calls, the most common raw scRGB colours, and Qt's grab for comparison. The project's reachable PC has no HDR monitor, so the owner ran it from the CI zip.
- **Report 1: the desktop path never ran.** In Qt 6, `QScreen::name()` is the monitor's model name ("VP2786-4K"), not the GDI device name, so no DXGI output matched and Qt's grab ran. `HdrCapture::deviceName()` now asks Windows for the monitor at the screen's top left corner.
- **Report 2 (two runs):**
  - The composed desktop (desktop duplication) has SDR white at exactly the SDR white level (5.0), for Explorer and Chrome alike. Dividing by it is right in both of Chrome's states.
  - Qt's GDI grab is right in Chrome's normal state but too bright in the other (GitHub's dark text `0.12` scRGB came out 97 to 131 instead of about 40). That is bug 9 as reported.
  - Windows.Graphics.Capture gives each window at its own scale: Explorer white 2.0, Chrome white 2.0 in its normal state and 5.0 in the other. 3.0.1 divided every window by 5.0, hence 2.5 times too dark.
- **Window capture fix:** `captureWindow` now takes the window frame and a desktop duplication frame. `HdrCapture::matchScale` compares the two over the window's visible part (per pixel, binned on a log scale; at least half the compared pixels must agree) and the frame is divided by SDR white / that ratio. When it cannot be compared (mostly covered, off screen, rotated panel), PrintWindow runs as before. Nothing about the display is hard-coded (owner's rule).
- **Yellow border:** on Windows 11, `IsBorderRequired(false)` is ignored until the app has asked `GraphicsCaptureAccess::RequestAccessAsync(Borderless)`; that is now asked once per run.
- **Tests:** `tst_hdrcapture` gains `matchScale` (a window 2.5 times dimmer than the desktop, a third covered, plus no match over other content and off the desktop) and `describeScRgb`. A window wholly covered by a single flat colour would match a wrong factor; noted, not handled.
- **Release:** the 3.0.1 draft and its tag were made from `4986f90`, before these fixes. See the owner's decision in the thread for whether 3.0.1 is re-tagged or this becomes 3.0.2.
