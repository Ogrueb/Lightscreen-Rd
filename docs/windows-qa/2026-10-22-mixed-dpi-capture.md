# Mixed-DPI capture and placement on two monitors (S04, M05; bugs 4, 5, 11, 12, 23, 45)

The five-step short version passed on 2026-10-29 (owner; whole screen 5760x2167, the area outside both monitors filled black as expected), except step 3, which found bug 45. Left to re-check, on a build with bug 45's fix:

Setup: two monitors, one at 100% and one at 150%, "Grab only the active monitor." off.

1. **Typed size on each monitor.** On the 150% monitor, draw an area, type `100x100`, Enter twice: the file is 150x150. On the 100% monitor the same gives 100x100. Try it with each monitor as the main display if you can.
