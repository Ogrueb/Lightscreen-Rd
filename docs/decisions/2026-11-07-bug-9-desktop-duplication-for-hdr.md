# 2026-11-07: bug 9, DXGI desktop duplication for HDR monitors only

- **Desktop duplication, not Windows.Graphics.Capture:** both return the composed desktop in FP16 scRGB. Duplication needs only D3D11 and DXGI (no WinRT, no capture border, no consent prompt) and returns a frame at once. Windows.Graphics.Capture would be the route for window capture if the owner's check shows that is affected too.
- **Only when the monitor is in HDR:** SDR monitors keep Qt's grab, so nothing changes for most users and the HDR path cannot break them. Any failure falls back to Qt's grab too.
- **Clip, no tone mapping:** SDR white maps to white and anything brighter clips. That matches what SDR content looks like on screen; HDR video or images in the capture lose their highlights, as in any 8-bit screenshot.
