# 48. An HDR capture can rarely fall back to the plain capture

- **Found:** by the owner on 2026-11-09: one capture on the PR #67 build had Chrome exactly 5 times too bright, the signature of Qt's GDI grab, which runs when the HDR path returns nothing. Not reproduced.
- **Likely causes:** desktop duplication losing access (a browser switching its swap chain into or out of HDR, a mode change) or delivering no frame, another program holding a duplication of the same output, or a plausibility check rejecting the result. Lost access and no frame are retried once with a new duplication since PR #67.
- **Diagnostics:** every HDR capture writes its trace to `lightscreen-hdr-capture-1.txt` in the temp folder (the last five are kept); `lightscreen --hdr-report` run straight after a bad capture prints them, with the call that failed.
- **Platform:** Windows with an HDR monitor. **Test:** none automated. **Status:** open, waiting for a log of a failing capture.
