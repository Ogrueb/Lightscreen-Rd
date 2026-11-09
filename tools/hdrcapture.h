/*
 * Copyright (C) 2026 Lightscreen Rd contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef HDRCAPTURE_H
#define HDRCAPTURE_H

#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QScreen>
#include <QString>
#include <qwindowdefs.h>

// Capture on a monitor in HDR mode (bug 9).
//
// With HDR on, Windows composes the desktop in scRGB: linear light, sRGB primaries, 1.0 = 80 nits, and SDR white at
// the "SDR content brightness" level (SDR white level / 1000 in scRGB units, 2.5 at 200 nits). Qt's screen grab
// (GDI) and PrintWindow get most windows right but can return a browser's hardware-composited content as if 1.0 were
// white, so it clips: a page comes out several times too bright, with coloured fringes where one channel clips before
// the others. Reading the desktop through DXGI desktop duplication, or one window through Windows.Graphics.Capture,
// in FP16 and dividing by the SDR white level gives what the screen shows for SDR content; HDR highlights above SDR
// white clip.
namespace HdrCapture {

// The kill switch (options/hdrCapture, "HDR capture: Automatic / Off"): when off, grabScreen and grabWindow return
// a null pixmap at once, so every capture takes the path Lightscreen always took. On by default; report() ignores it.
void setEnabled(bool enabled);
bool isEnabled();

// Crash tripwire. While an HDR capture runs, a marker file exists at this path; it is removed when the capture
// returns, whatever the result. A crash inside a graphics driver cannot be caught, so a marker found at the next
// start means the last capture died in the HDR path: takeCrashMarker() then returns true (and removes it), and the
// app turns HDR capture off. No marker is written while the path is empty (the default, and in the tests).
void setCrashMarkerPath(const QString &path);
bool takeCrashMarker();

// Converts FP16 RGBA scRGB pixels (DXGI_FORMAT_R16G16B16A16_FLOAT, rows bytesPerLine apart) to an opaque 8-bit sRGB
// image, scRGB value sdrWhite becoming white. Values above it clip to white, negative ones (colours outside sRGB) to 0.
// When flat is given, it receives the number of pixels that came out pure black or pure white (all three channels 0,
// or all 255), counted in the same loop.
QImage scRgbToSrgb(const void *pixels, int width, int height, int bytesPerLine, float sdrWhite, qint64 *flat = nullptr);

// Plausibility checks on an HDR result; each failing one sends the capture to the old path.
// The SDR white level Windows reports is believed only from 1.0 to 12.5 (80 to 1000 nits), a window's calibration
// factor (desktop / window) only from 0.2 to 10.
bool plausibleSdrWhite(float sdrWhite);
bool plausibleCalibration(float factor);
// A converted capture with 98 % or more of its pixels pure black or pure white came from a wrong scale.
bool plausibleFlatShare(qint64 flat, qint64 pixels);
// The extra check against Qt's (GDI) grab of the same screen, off by default because it costs a second grab
// (options/hdrCheckAgainstGdi, settings file only). GDI can only err bright, so an HDR result clearly brighter than it
// on average is wrong. True when the two cannot be compared (different sizes, null).
void setCheckAgainstGdi(bool on);
bool checkAgainstGdi();
bool notBrighterThanGdi(const QImage &hdr, const QImage &gdi);

// The monitor with this GDI device name (deviceName(), "\\.\DISPLAY1") captured through desktop duplication when
// it is in HDR mode. A null pixmap when it is not (SDR monitors keep Qt's grab), on other platforms, or when anything
// fails, so the caller falls back to Qt's grab.
QPixmap grabScreen(const QString &deviceName);

// The GDI device name of the monitor a screen is on ("\\.\DISPLAY1" on Windows; QScreen::name() elsewhere).
QString deviceName(const QScreen *screen);

// The window, when the monitor it is mostly on is in HDR mode, through Windows.Graphics.Capture in FP16: the whole
// window as Windows renders it (covered or partly off screen too), at the size Windows.Graphics.Capture gives. Each
// window's frame has its own scale, so the frame is calibrated against the composed desktop where the window is
// visible (matchScale). A null pixmap otherwise or on any failure, so the caller keeps PrintWindow.
QPixmap grabWindow(WId window);

// Whether no visible, uncloaked window above this one overlaps its visible frame, so the composed desktop shows all
// of it (grabWindow then cuts it from the desktop). False off Windows.
bool uncovered(WId window);

// Diagnostics for bug 9: for each screen and each visible Chrome/Edge window, whether the HDR path applies, the SDR
// white level, any failed call, and the raw scRGB values Windows hands over (most common colours), so the scaling can
// be checked against a page of known colours. Captures each screen and browser window once.
QString report();

// How much brighter the desktop is than the window placed on it at offset (both FP16 RGBA scRGB): desktop / window,
// from the pixels where the two agree on one factor. 0 when they overlap too little or more than half of the
// compared pixels disagree (the window is mostly covered).
float matchScale(const void *window, int width, int height, int bytesPerLine, const void *desktop, int desktopWidth,
                 int desktopHeight, int desktopBytesPerLine, const QPoint &offset);

// The most common colours and the largest channel value of FP16 RGBA scRGB pixels, one per line.
QString describeScRgb(const void *pixels, int width, int height, int bytesPerLine);

} // namespace HdrCapture

#endif // HDRCAPTURE_H
