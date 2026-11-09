# 2026-11-09: bug 9, safeguards on the HDR capture path

The HDR path runs graphics-driver code that cannot be tested on every machine. The review thread proposed safeguards; the owner decided each (2026-11-09):

- **Kill switch, approved:** `options/hdrCapture`, "HDR capture: Automatic / Off" on the Options tab, default Automatic. Off: `HdrCapture::grabScreen` and `grabWindow` return null at once and every capture takes the 3.0.0 path. Hidden off Windows.
- **Crash tripwire, approved:** a marker file (`lightscreen-hdr-capture-running` in the temp folder) exists while an HDR capture runs. One found at startup means the last run died inside the HDR path; the main window sets `options/hdrCapture` to Off and says so once.
- **Plausibility checks, approved only where free:** the SDR white level is believed from 1.0 to 12.5 (80 to 1000 nits), a window's calibration factor from 0.2 to 10, and a screen or calibrated-window capture with 98 % or more of its pixels pure black or white is rejected (counted in the conversion loop). A window cut from the desktop is not checked for that, since an empty document window is often almost all white. The comparison with Qt's GDI grab, which needs a second capture, is `options/hdrCheckAgainstGdi` in the settings file only, default off, screen and area captures only. Rejected as a default: running it on every capture ("too heavy").
- **Exceptions, approved:** any C++ exception in the HDR path (a failed 66 MB allocation) falls back.
- **Window frame wait, 500 ms:** the owner chose 500 ms over the proposed 300 ms, to be safe on slower hardware (was 1 s).
- **Logs, approved:** the last five capture logs are kept (`lightscreen-hdr-capture-1.txt` newest to `-5`); `--hdr-report` prints them.
