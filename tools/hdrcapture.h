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

// Converts FP16 RGBA scRGB pixels (DXGI_FORMAT_R16G16B16A16_FLOAT, rows bytesPerLine apart) to an opaque 8-bit sRGB
// image, scRGB value sdrWhite becoming white. Values above it clip to white, negative ones (colours outside sRGB) to 0.
QImage scRgbToSrgb(const void *pixels, int width, int height, int bytesPerLine, float sdrWhite);

// The monitor with this GDI device name (QScreen::name(), "\\.\DISPLAY1") captured through desktop duplication when
// it is in HDR mode. A null pixmap when it is not (SDR monitors keep Qt's grab), on other platforms, or when anything
// fails, so the caller falls back to Qt's grab.
QPixmap grabScreen(const QString &deviceName);

// The window, when the monitor it is mostly on is in HDR mode, through Windows.Graphics.Capture in FP16 with that
// monitor's SDR white level: the whole window as Windows renders it (covered or partly off screen too), at the size
// Windows.Graphics.Capture gives. A null pixmap otherwise or on any failure, so the caller keeps PrintWindow.
QPixmap grabWindow(WId window);

} // namespace HdrCapture

#endif // HDRCAPTURE_H
