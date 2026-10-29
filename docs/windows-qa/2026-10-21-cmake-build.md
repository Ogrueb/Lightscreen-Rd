# CMake build on Windows (PR #44, M11)

Build, exe properties and the deployed app passed on 2026-10-29 (a Claude desktop session on the owner's PC). `ctest` failed two `tst_os` checks at the owner's display scaling, a test bug (bug 43) fixed in PR #59. Left:

1. **Tests.** Pull `master`, rebuild with the "Windows > Build" commands in `docs/build.md`, then run `ctest --preset windows-msvc-release` in the same prompt: 100% tests passed.
