# Window capture uses the visible window bounds (PR #18, bug 5)

Steps 1 and 2 (a window next to another, a maximized window) passed on 2026-10-23 (owner). Step 3 and the window picker found bug 35. On 2026-10-29 the picker still chose the window behind at the owner's scaling (bug 42, fixed since), and step 1 was run with Notepad instead of Explorer (passed). Left to re-check, with a real mouse:

1. Move Explorer partly off screen to the left or top and take an active-window capture: the whole window, title bar and toolbar included (not black), clean edges, nothing cut.
2. Use the window picker on Explorer, then on a browser or the Settings app, with another window behind each: the file shows the picked window's own title bar and content, not the window behind or the picker.
3. Note what the four rounded corners look like on Windows 11 (outside the window's shape): report only if it looks wrong.
