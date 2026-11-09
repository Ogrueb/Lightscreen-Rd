# Browser captured too bright on an HDR monitor (PRs #65 and #67, bug 9)

On the HDR monitor, with the CI build's `lightscreen-win64` zip from PR #67 (or `master` once merged). Use a page with colour, such as a GitHub repository page in Chrome, and check both of Chrome's states: `lightscreen --hdr-report` shows which one Chrome is in (its window frame with white at 2.0, or at the SDR white level when it is in the state that 3.0.0 captured too bright).

To put Chrome in that other state on purpose, play an HDR video in it (for example a YouTube clip marked HDR, at a quality with "HDR" in its name) and leave the tab open; run `lightscreen --hdr-report` again and check that the Chrome window's white is now at the SDR white level (5.0 at 400 nits) instead of 2.0. Starting Chrome with `--force-color-profile=scrgb-linear` may also do it (not verified). If neither works, check whichever state Chrome is in and say so.

1. Capture the whole screen, then the current monitor, then an area over the Chrome tab. Each file matches the screen: no washed-out colours, no red, cyan or yellow fringes on text and icons. Other windows in the full-screen capture look as before.
2. Change Windows' "SDR content brightness" (Settings > System > Display > HDR) and capture again. The capture still matches the screen.
3. Turn HDR off and capture the whole screen. As before this change (SDR monitors keep the old capture path).
4. Capture the Chrome window with the window picker and with "Active window". Both match the screen, show the whole window with no pointer in the file, and no yellow border flashes, and have the same edges as before (no extra transparent or black margin). Repeat with Chrome partly covered by another window (less than half): the capture shows Chrome alone, at the right brightness (a yellow border may flash here if Windows ignores the borderless request; `--hdr-report` shows whether it was refused).
5. Capture an Explorer window the same way: it matches the screen too (not darker, as in 3.0.1).
6. Turn HDR off and capture the Chrome window: as before this change.
