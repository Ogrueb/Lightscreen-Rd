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
#ifndef CAPTUREGEOMETRY_H
#define CAPTUREGEOMETRY_H

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>

// Pure functions behind desktop capture and area selection on screens with different scale factors.
//
// Coordinates, as Qt 6 defines them:
// - "logical" is QScreen::geometry() and QCursor::pos(). Each screen keeps its native top left corner and has its
//   size divided by its own device pixel ratio, so screens at different ratios leave gaps (or overlap) in logical
//   space.
// - "device" is the native desktop in physical pixels: a screen covers QRect(logical top left, native size).
// - A window's own (widget) coordinates are its native area divided by that window's single device pixel ratio,
//   even where the window extends onto a screen with another ratio.
namespace CaptureGeometry {

struct Screen {
    QRect geometry;   // logical, QScreen::geometry()
    qreal ratio = 1;  // QScreen::devicePixelRatio()
    QSize deviceSize; // native size; when empty, geometry.size() * ratio rounded
};

// The screen's rectangle on the native desktop.
QRect deviceRect(const Screen &screen);

// The smallest rectangle on the native desktop covering every screen.
QRect deviceUnion(const QList<Screen> &screens);

// The index of the screen whose logical geometry contains the point, or -1.
int screenAt(const QList<Screen> &screens, const QPoint &logical);

// A global logical point (QCursor::pos()) on the native desktop, through the screen that contains it.
// A point on no screen is returned unchanged.
QPoint logicalToDevice(const QList<Screen> &screens, const QPoint &logical);

// The index of the screen that holds the largest part of the device rectangle (the screen Windows assigns a window
// to), or -1 when none does. Ties go to the first screen.
int dominantScreen(const QList<Screen> &screens, const QRect &device);

// The logical geometry to give a window on the screen so that it covers the device rectangle, rounded outward:
// a window's widget coordinates scale its native area by its own screen's ratio around that screen's top left.
QRect windowGeometry(const Screen &screen, const QRect &device);

// A selection in a window's coordinates as device pixels relative to the window: the near edges rounded to the
// nearest device pixel and the size scaled and rounded, so a selection keeps its size times the ratio (a typed
// 100x100 at 125% is always 125x125) and its edges move by at most half a device pixel. Not normalized first; a
// selection with a positive size is clipped to the bounds when they are not empty.
QRect selectionToDevice(const QRect &selection, qreal ratio, const QSize &bounds = QSize());

// A size typed in the area dialog ("100x100") on the native desktop. It is meant in the logical pixels of the screen
// at the device point (the selection's top left), so it is scaled by that screen's ratio, not by the dialog window's
// single ratio (the window spans every screen; bug 45). Off every screen, fallbackRatio is used.
QSize typedSizeToDevice(const QList<Screen> &screens, const QPoint &device, const QSize &typed, qreal fallbackRatio);

} // namespace CaptureGeometry

#endif // CAPTUREGEOMETRY_H
