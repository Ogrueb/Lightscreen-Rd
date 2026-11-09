# 2026-11-09: bug 9, covered windows keep Windows.Graphics.Capture on Windows 10

- **Context:** the owner's HDR machine runs Windows 10 (likely 21H2), where Windows.Graphics.Capture always draws a yellow border around the captured window and offers no way to turn it off. Lightscreen 2.5 never showed it, since it captured windows through GDI only.
- **Decision (owner, 2026-11-09):** an uncovered window is cut from the desktop duplication frame (no border); a covered window on an HDR monitor keeps the calibrated Windows.Graphics.Capture path, so its colours are right but the border can flash briefly. It never appears in the saved file.
- **Rejected:** PrintWindow for covered windows on Windows 10. No border, but a covered Chrome window in its HDR state would be captured several times too bright again (bug 9).
- Listed as a known issue in `docs/release-notes/v3.0.1.md`.
