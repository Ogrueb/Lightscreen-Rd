# Browser captured too bright on an HDR monitor (PR #N, bug 9)

On the HDR monitor, with the CI build's `lightscreen-win64` zip from this PR (or `master` once merged). Use a page with colour, such as a GitHub repository page in Chrome, and keep Chrome where the 3.0 build showed the problem.

1. Capture the whole screen, then the current monitor, then an area over the Chrome tab. Each file matches the screen: no washed-out colours, no red, cyan or yellow fringes on text and icons. Other windows in the full-screen capture look as before.
2. Change Windows' "SDR content brightness" (Settings > System > Display > HDR) and capture again. The capture still matches the screen.
3. Turn HDR off and capture the whole screen. As before this change (SDR monitors keep the old capture path).
4. Information only: capture the Chrome window with the window picker and with "Active window". Say whether those are still too bright; window capture was not changed.
