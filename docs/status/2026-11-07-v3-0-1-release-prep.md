# 2026-11-07: v3.0.1 prepared (bug 9 fix)

- The owner approved merging PR #65 (bug 9, HDR capture) once CI passed, and asked for a release draft. PR #65 was merged at `5509ad1`.
- Version 3.0.1: `CMakeLists.txt` `project(... VERSION 3.0.1)`, `CHANGELOG.md` `[3.0.1] - 2026-11-07`, draft text in `docs/release-notes/v3.0.1.md`.
- **Not yet verified on HDR hardware:** `docs/windows-qa/2026-11-07-hdr-browser-capture.md` is open. Run it on the draft release's zip before publishing.
- Next: the tag `v3.0.1` on `master` starts the release workflow, which opens the draft release with `lightscreen-rd-3.0.1-win64.zip`. The owner publishes it with the notes above.
