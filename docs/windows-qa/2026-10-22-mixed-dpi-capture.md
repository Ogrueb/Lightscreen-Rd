# Mixed-DPI capture and placement on two monitors (S04, M05; bugs 4, 5, 11, 12, 23)

The short version agreed on 2026-10-29; it replaces the longer lists of this check and of `2026-10-15-qscreen-capture-and-placement.md`. Single-monitor scaling is covered by the passed fractional-scaling check, and the geometry by `tst_capturegeometry`.

Setup: two monitors, one at 100% and one at 150% (Settings > System > Display: click each monitor, then Scale). Options tab > Screenshots: "Grab only the active monitor." off.

1. **Whole screen.** The file's width is both monitors' native widths added (for example 1920 + 2560 = 4480), both halves sharp, nothing doubled or cut at the join.
2. **Area across the join.** The area dialog's desktop lines up with the real one on both monitors (no jump or zoom). A rectangle crossing from one monitor to the other saves exactly what was marked, with no step at the join.
3. **Typed size on the 150% monitor.** Draw an area there, type `100x100`, Enter twice: the file is 150x150.
4. **Window on the second monitor.** Active-window capture of a window on the non-main monitor: the window whole, no strip of what is behind it.
5. **Where windows open.** With the cursor on the second monitor, open the window picker: it appears centred on that monitor.
