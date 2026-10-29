# 44. A strip at the main window's right edge after lowering the display scaling

- **Found:** by the owner on 2026-10-29 (fractional-scaling check, C1): after lowering the scale (for example 150% to 125%) while the app runs, a strip of leftover drawing shows along the main window's right edge. Reopening the window turns it into a solid white bar; any manual resize clears it.
- **Cause:** not investigated. Most likely the window's backing store is not fully redrawn when Windows resizes the window for the new scale (Qt on Windows, per-monitor DPI awareness since Stage 7's exact-scale change).
- **Not fixed (owner, 2026-10-29):** cosmetic, rare (only when the scale changes while the app runs), and gone on any resize. A fix would react to the scale change with a resize or full repaint. Checking it needs another scaling change on Windows.
- **Workaround:** resize the main window slightly, or restart the app.
- **Platform:** Windows. **Status:** known minor issue for 3.0.
