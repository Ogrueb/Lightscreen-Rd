# Mixed-DPI capture and placement on two monitors (S04, M05; bugs 4, 5, 11, 12, 23, 45)

The five-step short version passed on 2026-10-29 (owner; whole screen 5760x2167, the area outside both monitors filled black as expected), except the typed size, which found bug 45 and then its follow-up (the size tag and the typed text). Left to re-check, on a build with the follow-up:

Setup: two monitors at different scaling, "Grab only the active monitor." off.

1. **Typed size on the second monitor.** Draw an area on the non-main monitor and type `100x100`: the typed text appears at that area, not on the main monitor. Press Enter: the area's size tag reads `100x100`. Press Enter again: the file is 100x100 times that monitor's scaling (for example 125x125 at 125%).
