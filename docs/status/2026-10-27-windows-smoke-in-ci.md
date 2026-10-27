# 2026-10-27: Windows smoke test in CI and in the release workflow

- **Probe:** two runs of a throwaway workflow on the thread's branch showed that a real desktop is available on `windows-latest`. The probe files are removed again; the findings are in `docs/decisions/2026-10-27-windows-ui-checks-ci-or-desktop-session.md`.
- **`.github/scripts/windows-smoke.ps1`** (new) runs a copy of an app folder in portable mode:
  - start in the tray;
  - press F9 from outside until a PNG appears (20 s at most);
  - the PNG has the screen's size;
  - `--quit` ends the app within 10 s.
- **`ci.yml`, Windows job:** a "Windows smoke" step after the optimizers are added, on a copy of `deploy`, so the uploaded artifact holds no test `config.ini`.
- **`release.yml`, package job:** "Check the zip" unpacks the zip and checks it holds exactly the one folder, with the exe, the Qt DLLs and plugins (`platforms`, `imageformats\qwebp.dll`, `sqldrivers`, `tls`), both optimizers and their licences. It then runs the smoke on the unpacked app. This PR changes `release.yml`, so the package job runs on it.
- **Docs:** `docs/build.md` describes both steps. `docs/windows-qa/2026-10-21-release-dry-run.md` drops the zip-contents step, which the workflow now covers.
