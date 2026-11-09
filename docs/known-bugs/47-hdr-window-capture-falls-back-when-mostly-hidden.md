# 47. With HDR on, some windows are captured the old way, which can be too bright for a browser

- **Found:** by design in the bug 9 fix (PR #67, 2026-11-09).
- **What happens:** on a monitor in HDR mode, a window is cut from the HDR screen capture when it is uncovered, or read through Windows.Graphics.Capture and matched to the screen's brightness where it is visible. When neither works, the window is captured with PrintWindow as in 3.0.0, which for a browser in its HDR state is several times too bright (bug 9). That happens when the window is mostly covered (less than half of it can be compared with the screen), mostly off screen, on a rotated monitor, spans two monitors (the monitor it is mostly on is used and the comparison may fail), or when a plausibility check rejects the result.
- **Workaround:** bring the window to the front, or capture an area or the whole screen, which always use the HDR path.
- **Platform:** Windows with an HDR monitor. **Test:** `tst_hdrcapture::matchScale*` cover the comparison; the rest needs an HDR monitor (`docs/windows-qa/2026-11-07-hdr-browser-capture.md`). **Status:** known limit.
