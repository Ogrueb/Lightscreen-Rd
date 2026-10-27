# 2026-10-27: Windows UI checks, split between CI and a Claude desktop session

**Decision (owner approved the PR, 2026-10-27).** Two Windows checks move into CI, where they run on every pull request and release run: the tray hotkey (bug 33) in `ci.yml`, and the release zip's contents plus a run of the app from it in `release.yml`. Both use `.github/scripts/windows-smoke.ps1`. The rest of the 3.0 Windows sign-off is run by a Claude session in the Claude desktop app on the owner's PC (computer use), and by the owner for what neither can do. The split is in the owner's test plan (a Claude doc), "Who runs what".

**Why.** A probe workflow (2026-10-27, runs 40465502914 and 40465674069) showed that GitHub's `windows-latest` runner has a real signed-in desktop session (session 2, console, Explorer running, one 1024x768 screen). There:
- a hotkey registered to the thread fires on a `keybd_event` press;
- `PrintWindow(PW_RENDERFULLCONTENT)` captures a window partly off screen correctly;
- the deployed app, started in the tray, saves a whole-screen PNG on F9 and quits on `--quit`.

A Print Screen sent with `keybd_event` never reached the app, so CI uses F9, and the Print Screen check stays with the owner. Computer use also sends synthetic keys.

The runner is Windows Server at 100% scale, not the owner's Windows 11, so a CI pass is weaker evidence for a release than a pass on the owner's PC. CI's strength is repeating a check on every change. It therefore takes the two checks that are cheap to automate and guard the worst regression of the port (bug 33) and the package that users download. Visual checks (dark mode, Windows 11 window corners), your own apps and build tools stay with the desktop session. Display scaling, a second monitor, signing out and publishing stay with the owner.

**Not done.** Dark-mode screenshots, the About link and the start-at-login entry in CI. On Windows Server they would show less than the desktop session does on Windows 11.
