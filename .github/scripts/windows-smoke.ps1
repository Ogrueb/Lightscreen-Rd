# Windows smoke test on a GitHub-hosted runner, which has a real desktop session (checked by the desktop probe,
# 2026-10-27): Lightscreen Rd starts in the tray, the whole-screen hotkey pressed from outside the app saves a PNG of
# the whole screen (bug 33: hotkeys from the tray), and `lightscreen --quit` ends it.
#
# The app folder is copied first, so the folder that gets uploaded or zipped never holds the test's config.ini.
# F9, not Print Screen: a Print Screen sent with keybd_event never reached the app on the runner.
param(
    [Parameter(Mandatory)] [string]$App,
    [Parameter(Mandatory)] [string]$Work
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SmokeInput {
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
'@

function Fail($message) {
    Write-Host "::error::$message"
    exit 1
}

New-Item -ItemType Directory -Force $Work | Out-Null
$Work = (Resolve-Path $Work).Path
$copy = Join-Path $Work 'app'
$shots = Join-Path $Work 'shots'
Copy-Item -Recurse (Resolve-Path $App).Path $copy
New-Item -ItemType Directory -Force $shots | Out-Null
$exe = Join-Path $copy 'lightscreen.exe'

# Portable mode (config.ini next to the exe): no user settings are touched, the first-run dialog is skipped, and the
# updater makes no network calls.
@"
[file]
format=0
enabled=true
target=$($shots -replace '\\', '/')

[options]
tray=true
disableUpdater=true

[actions]
screen\enabled=true
screen\hotkey=F9
"@ | Set-Content -Encoding ASCII (Join-Path $copy 'config.ini')

$process = Start-Process $exe -PassThru
$file = $null

# The hotkeys are registered once the app is up; press F9 every two seconds until a screenshot appears (20 s at most).
for ($i = 0; $i -lt 10 -and -not $file; $i++) {
    Start-Sleep -Seconds 2
    if ($process.HasExited) { Fail "lightscreen.exe exited on its own (code $($process.ExitCode))" }
    [SmokeInput]::keybd_event(0x78, 0, 0, [UIntPtr]::Zero)
    [SmokeInput]::keybd_event(0x78, 0, 2, [UIntPtr]::Zero)
    for ($j = 0; $j -lt 4 -and -not $file; $j++) {
        Start-Sleep -Milliseconds 500
        $file = Get-ChildItem $shots -Filter *.png | Select-Object -First 1
    }
}

if (-not $file) {
    $process.Kill()
    Fail 'the whole-screen hotkey (F9) saved no screenshot within 20 seconds'
}

$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$image = [System.Drawing.Image]::FromFile($file.FullName)
$size = "$($image.Width)x$($image.Height)"
$image.Dispose()
Write-Host "Saved $($file.Name), $size; the screen is $($screen.Width)x$($screen.Height)"

if ($size -ne "$($screen.Width)x$($screen.Height)") {
    $process.Kill()
    Fail "the screenshot is $size, not the screen's $($screen.Width)x$($screen.Height)"
}

$quit = Start-Process $exe -ArgumentList '--quit' -PassThru
if (-not $quit.WaitForExit(10000)) { Fail 'lightscreen --quit did not return within 10 seconds' }

if (-not $process.WaitForExit(10000)) {
    $process.Kill()
    Fail 'lightscreen.exe did not quit within 10 seconds of --quit'
}

Write-Host 'Windows smoke passed'
