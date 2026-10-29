# PNG optimizers: re-checks (S05, bugs 10 and 38)

Steps 1 to 4 passed in the 2026-10-23 pass (unoptimized 318 KB, OptiPNG 200 KB, oxipng 170 KB). Use a PR #58 build or later, with Options tab > Screenshots > "Optimize PNG screenshots." on and format PNG (General tab).

1. **Quit during a burst (bug 38).** Take a burst of screenshots and quit Lightscreen at once from the tray menu (confirm the prompt): the files open, and no `*.optimize.tmp` file is in the screenshot folder, also after a few seconds. Start Lightscreen again: the remaining files get optimized ("N optimizing").
2. **No optimizer at all (optional).** Close Lightscreen, rename both `oxipng.exe` and `optipng.exe` (for example to `.off`), start it again: in Options the optimize checkbox is greyed out, the tool box next to it is hidden, and "No optimizer found next to lightscreen.exe" shows in full (it was cut off on 2026-10-29), and screenshots are still saved, unoptimized. Rename both back.
