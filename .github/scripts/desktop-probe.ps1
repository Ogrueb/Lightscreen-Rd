# Probes the runner's desktop: session, screens, a global hotkey pressed with synthetic input, window capture of a
# window partly off screen, and Lightscreen Rd taking a screenshot on its Print Screen hotkey from the tray.
param([string]$App, [string]$Out)

$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$results = [ordered]@{}

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Threading;

public static class Probe {
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] public struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam, lParam; public uint time; public POINT pt; }

    [DllImport("user32.dll", SetLastError = true)] static extern bool RegisterHotKey(IntPtr hWnd, int id, uint mods, uint vk);
    [DllImport("user32.dll")] static extern bool UnregisterHotKey(IntPtr hWnd, int id);
    [DllImport("user32.dll")] static extern bool PeekMessage(out MSG msg, IntPtr hWnd, uint min, uint max, uint remove);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();

    public static bool InputDesktop() {
        IntPtr d = OpenInputDesktop(0, false, 0x0001);
        if (d == IntPtr.Zero) return false;
        CloseDesktop(d);
        return true;
    }

    // F9 registered to the thread (as UGlobalHotkeys does since bug 33), pressed with keybd_event.
    public static string Hotkey() {
        if (!RegisterHotKey(IntPtr.Zero, 1, 0x4000, 0x78)) return "RegisterHotKey failed: " + Marshal.GetLastWin32Error();
        keybd_event(0x78, 0, 0, UIntPtr.Zero);
        keybd_event(0x78, 0, 2, UIntPtr.Zero);
        try {
            for (int i = 0; i < 30; ++i) {
                MSG msg;
                if (PeekMessage(out msg, IntPtr.Zero, 0x0312, 0x0312, 1)) return "WM_HOTKEY received";
                Thread.Sleep(100);
            }
            return "no WM_HOTKEY within 3 s";
        } finally { UnregisterHotKey(IntPtr.Zero, 1); }
    }

    public static Bitmap Print(IntPtr hwnd, int w, int h) {
        Bitmap b = new Bitmap(w, h);
        using (Graphics g = Graphics.FromImage(b)) {
            IntPtr dc = g.GetHdc();
            PrintWindow(hwnd, dc, 2);
            g.ReleaseHdc(dc);
        }
        return b;
    }

    public static bool IsForeground(IntPtr hwnd) { return GetForegroundWindow() == hwnd; }
}
'@

function Report($name, $value) { $results[$name] = "$value"; Write-Host "${name}: $value" }

# 1. Session and screens.
Report 'User' (whoami)
Report 'Session id' (Get-Process -Id $PID).SessionId
Report 'UserInteractive' ([Environment]::UserInteractive)
Report 'Input desktop reachable' ([Probe]::InputDesktop())
Report 'Screens' ((([System.Windows.Forms.Screen]::AllScreens) | ForEach-Object { "$($_.DeviceName) $($_.Bounds.Width)x$($_.Bounds.Height) at $($_.Bounds.X),$($_.Bounds.Y)" }) -join '; ')
Report 'Explorer running' ([bool](Get-Process explorer -ErrorAction SilentlyContinue))
query user 2>&1 | ForEach-Object { Write-Host "query user: $_" }

# 2. Global hotkey with synthetic input.
Report 'Hotkey (F9, synthetic press)' ([Probe]::Hotkey())

# 3. A red window, partly off screen to the left: what PrintWindow and a screen copy see.
$form = New-Object System.Windows.Forms.Form
$form.StartPosition = 'Manual'
$form.Location = New-Object System.Drawing.Point(-150, 100)
$form.Size = New-Object System.Drawing.Size(400, 300)
$form.BackColor = [System.Drawing.Color]::Red
$form.Text = 'Probe window'
$form.TopMost = $true
$form.Show()
for ($i = 0; $i -lt 20; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 }
$form.Activate()
[System.Windows.Forms.Application]::DoEvents()
Report 'Probe window is foreground' ([Probe]::IsForeground($form.Handle))

$printed = [Probe]::Print($form.Handle, 400, 300)
$printed.Save("$Out\printwindow.png")
Report 'PrintWindow pixel on screen part (300,150)' $printed.GetPixel(300, 150)
Report 'PrintWindow pixel off screen part (50,150)' $printed.GetPixel(50, 150)

$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$screen = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
$g = [System.Drawing.Graphics]::FromImage($screen)
try {
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $screen.Save("$Out\screen.png")
    Report 'Screen copy pixel inside probe window (100,250)' $screen.GetPixel(100, 250)
} catch { Report 'Screen copy' "failed: $($_.Exception.Message)" }
$g.Dispose()
$form.Close()

# 4. Lightscreen Rd from the tray: the whole-screen hotkey saves a PNG. Tried with Print Screen and with F9; the
# desktop is saved after the start and after the press, so a dialog the app shows is visible in the artifact.
function SaveScreen($path) {
    $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap($b.Width, $b.Height)
    $gr = [System.Drawing.Graphics]::FromImage($bmp)
    $gr.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
    $bmp.Save($path)
    $gr.Dispose(); $bmp.Dispose()
}

$exe = (Resolve-Path "$App\lightscreen.exe").Path
$documents = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Screenshots'

foreach ($key in @(@{ Name = 'Print'; Vk = 0x2C }, @{ Name = 'F9'; Vk = 0x78 })) {
    $shots = "$Out\shots-$($key.Name)"
    New-Item -ItemType Directory -Force $shots | Out-Null
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
screen\hotkey=$($key.Name)
"@ | Set-Content -Encoding ASCII "$App\config.ini"

    $proc = Start-Process $exe -PassThru
    Start-Sleep -Seconds 6
    Report "Lightscreen ($($key.Name)) running after 6 s" (-not $proc.HasExited)
    SaveScreen "$Out\lightscreen-$($key.Name)-started.png"
    [Probe]::keybd_event([byte]$key.Vk, 0, 0, [UIntPtr]::Zero)
    [Probe]::keybd_event([byte]$key.Vk, 0, 2, [UIntPtr]::Zero)
    $file = $null
    for ($i = 0; $i -lt 20 -and -not $file; $i++) {
        Start-Sleep -Milliseconds 500
        $file = Get-ChildItem $shots, $documents -Recurse -Include *.png, *.jpg -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    SaveScreen "$Out\lightscreen-$($key.Name)-pressed.png"
    if ($file) {
        $img = [System.Drawing.Image]::FromFile($file.FullName)
        Report "Lightscreen hotkey $($key.Name) from the tray" "saved $($file.FullName), $($img.Width)x$($img.Height)"
        $img.Dispose()
    } else {
        Report "Lightscreen hotkey $($key.Name) from the tray" 'no file within 10 s'
    }
    Get-Content "$App\config.ini" | ForEach-Object { Write-Host "config.ini after run: $_" }
    $quit = Start-Process $exe -ArgumentList '--quit' -PassThru
    $quit.WaitForExit(10000) | Out-Null
    Report "Lightscreen ($($key.Name)) exited after --quit" ($proc.WaitForExit(10000))
    if (-not $proc.HasExited) { $proc.Kill() }
}

# Summary.
$summary = "## Desktop probe`n`n| Check | Result |`n| --- | --- |`n"
foreach ($k in $results.Keys) { $summary += "| $k | $($results[$k] -replace '\|', '/') |`n" }
$summary | Out-File -Append -Encoding utf8 $env:GITHUB_STEP_SUMMARY
$summary | Out-File -Encoding utf8 "$Out\results.md"
exit 0
