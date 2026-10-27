# Release workflow dry run (M15)

Before tagging v3.0.0. A thread can start the run for you; otherwise: GitHub, Actions tab, "Release", "Run workflow" on `master`. The workflow itself now checks what is in the zip and runs the app from it (start, a hotkey from the tray, `--quit`), so a green run covers what steps 2 and 3 used to check by hand, except SmartScreen.

1. **Draft.** The run is green and the Releases page shows a draft pre-release "Dry run N (Lightscreen Rd 3.0, master)" with `lightscreen-rd-3.0-win64.zip` attached.
2. **Runs on your PC.** Download the zip, unzip it and start `lightscreen.exe`: it starts without a UAC prompt (SmartScreen may warn, since releases are unsigned) and takes a whole-screen screenshot.
3. **Clean up.** Delete the draft (Releases page, the draft's "..." menu, Delete). Do not publish it.
