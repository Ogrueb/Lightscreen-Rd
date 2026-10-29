// Tests for tools/capturegeometry (S04): mapping between Qt's logical screen geometry, the native desktop and a
// window's own coordinates, on synthetic screen layouts. Each screen's logical top left is its native top left and
// its logical size is the native size divided by its ratio (QScreen::geometry() in Qt 6).
#include <QtTest>

#include <tools/capturegeometry.h>

using CaptureGeometry::Screen;
typedef QList<Screen> Layout;
Q_DECLARE_METATYPE(Screen)
Q_DECLARE_METATYPE(Layout)

namespace {
// 1920x1080 at 100% on the left, 2560x1440 at 150% on its right (logical 1707x960, 2560 / 1.5 rounded).
Layout sideBySide()
{
    return { { QRect(0, 0, 1920, 1080), 1, QSize(1920, 1080) },
             { QRect(1920, 0, 1707, 960), 1.5, QSize(2560, 1440) } };
}

// The primary 2560x1440 at 150% at the origin, 1920x1080 at 100% to its left and 200 pixels lower.
Layout negativeOffset()
{
    return { { QRect(0, 0, 1707, 960), 1.5, QSize(2560, 1440) },
             { QRect(-1920, 200, 1920, 1080), 1, QSize(1920, 1080) } };
}

// The primary 1920x1080 at 100%, a 3840x2160 screen at 200% above it. Logical y from -1080 to 0 is on no screen.
Layout above()
{
    return { { QRect(0, 0, 1920, 1080), 1, QSize(1920, 1080) },
             { QRect(0, -2160, 1920, 1080), 2, QSize(3840, 2160) } };
}
}

class tst_CaptureGeometry : public QObject
{
    Q_OBJECT

private slots:
    void deviceRect_data()
    {
        QTest::addColumn<Screen>("screen");
        QTest::addColumn<QRect>("expected");

        QTest::newRow("100%") << Screen{ QRect(0, 0, 1920, 1080), 1, QSize() } << QRect(0, 0, 1920, 1080);
        QTest::newRow("150%, native size given") << Screen{ QRect(1920, 0, 1707, 960), 1.5, QSize(2560, 1440) }
                                                 << QRect(1920, 0, 2560, 1440);
        // Without the grabbed size, the logical size times the ratio, rounded: one pixel too many here.
        QTest::newRow("150%, size estimated") << Screen{ QRect(1920, 0, 1707, 960), 1.5, QSize() }
                                              << QRect(1920, 0, 2561, 1440);
        QTest::newRow("125%, negative offset") << Screen{ QRect(-1536, -100, 1536, 864), 1.25, QSize() }
                                               << QRect(-1536, -100, 1920, 1080);
    }

    void deviceRect()
    {
        QFETCH(Screen, screen);
        QFETCH(QRect, expected);

        QCOMPARE(CaptureGeometry::deviceRect(screen), expected);
    }

    void deviceUnion_data()
    {
        QTest::addColumn<Layout>("layout");
        QTest::addColumn<QRect>("expected");

        QTest::newRow("no screens") << Layout() << QRect();
        QTest::newRow("one screen") << Layout{ sideBySide().at(1) } << QRect(1920, 0, 2560, 1440);
        QTest::newRow("100% and 150%") << sideBySide() << QRect(0, 0, 4480, 1440);
        QTest::newRow("negative offset") << negativeOffset() << QRect(-1920, 0, 4480, 1440);
        QTest::newRow("above the primary") << above() << QRect(0, -2160, 3840, 3240);
    }

    void deviceUnion()
    {
        QFETCH(Layout, layout);
        QFETCH(QRect, expected);

        QCOMPARE(CaptureGeometry::deviceUnion(layout), expected);
    }

    void logicalToDevice_data()
    {
        QTest::addColumn<Layout>("layout");
        QTest::addColumn<QPoint>("logical");
        QTest::addColumn<QPoint>("expected");

        QTest::newRow("100% screen") << sideBySide() << QPoint(100, 50) << QPoint(100, 50);
        QTest::newRow("150% screen, scaled from its corner") << sideBySide() << QPoint(2020, 50) << QPoint(2070, 75);
        QTest::newRow("150% screen, corner") << sideBySide() << QPoint(1920, 0) << QPoint(1920, 0);
        QTest::newRow("150% screen, last pixel") << sideBySide() << QPoint(3626, 959) << QPoint(4479, 1438);
        QTest::newRow("negative offset, left screen") << negativeOffset() << QPoint(-100, 300) << QPoint(-100, 300);
        QTest::newRow("negative offset, primary") << negativeOffset() << QPoint(1000, 500) << QPoint(1500, 750);
        QTest::newRow("above, upper screen") << above() << QPoint(960, -1620) << QPoint(1920, -1080);
        QTest::newRow("above, in the logical gap") << above() << QPoint(100, -500) << QPoint(100, -500);
    }

    void logicalToDevice()
    {
        QFETCH(Layout, layout);
        QFETCH(QPoint, logical);
        QFETCH(QPoint, expected);

        QCOMPARE(CaptureGeometry::logicalToDevice(layout, logical), expected);
    }

    void screenAt()
    {
        QCOMPARE(CaptureGeometry::screenAt(sideBySide(), QPoint(0, 0)), 0);
        QCOMPARE(CaptureGeometry::screenAt(sideBySide(), QPoint(1920, 0)), 1);
        // Logical x 3627 is past the 150% screen's logical width, although native pixel 3627 is on it.
        QCOMPARE(CaptureGeometry::screenAt(sideBySide(), QPoint(3627, 0)), -1);
        QCOMPARE(CaptureGeometry::screenAt(negativeOffset(), QPoint(-1920, 0)), -1);
        QCOMPARE(CaptureGeometry::screenAt(above(), QPoint(0, -2160)), 1);
    }

    void dominantScreen()
    {
        QCOMPARE(CaptureGeometry::dominantScreen(sideBySide(), QRect(0, 0, 4480, 1440)), 1);
        QCOMPARE(CaptureGeometry::dominantScreen(sideBySide(), QRect(0, 0, 1920, 1080)), 0);
        QCOMPARE(CaptureGeometry::dominantScreen(negativeOffset(), QRect(-1920, 0, 4480, 1440)), 0);
        QCOMPARE(CaptureGeometry::dominantScreen(above(), QRect(0, -2160, 3840, 3240)), 1);
        QCOMPARE(CaptureGeometry::dominantScreen(sideBySide(), QRect(-500, -500, 10, 10)), -1);
        QCOMPARE(CaptureGeometry::dominantScreen(Layout(), QRect(0, 0, 10, 10)), -1);
    }

    // The window geometry, mapped back the way Qt maps a window on that screen, covers the device rectangle.
    void windowGeometry_data()
    {
        QTest::addColumn<Screen>("screen");
        QTest::addColumn<QRect>("device");
        QTest::addColumn<QRect>("expected");

        QTest::newRow("100% screen, whole desktop") << sideBySide().at(0) << QRect(0, 0, 4480, 1440)
                                                    << QRect(0, 0, 4480, 1440);
        // Native 0 is 1920 pixels left of the 150% screen's corner: 1280 logical. Native 4480 is 1706.67 logical
        // right of it, rounded up.
        QTest::newRow("150% screen, whole desktop") << sideBySide().at(1) << QRect(0, 0, 4480, 1440)
                                                    << QRect(640, 0, 2987, 960);
        QTest::newRow("150% screen, itself") << sideBySide().at(1) << QRect(1920, 0, 2560, 1440)
                                             << QRect(1920, 0, 1707, 960);
        QTest::newRow("150% primary, negative offset") << negativeOffset().at(0) << QRect(-1920, 0, 4480, 1440)
                                                       << QRect(-1280, 0, 2987, 960);
        QTest::newRow("200% screen above, whole desktop") << above().at(1) << QRect(0, -2160, 3840, 3240)
                                                          << QRect(0, -2160, 1920, 1620);
    }

    void windowGeometry()
    {
        QFETCH(Screen, screen);
        QFETCH(QRect, device);
        QFETCH(QRect, expected);

        const QRect geometry = CaptureGeometry::windowGeometry(screen, device);
        QCOMPARE(geometry, expected);

        // Qt's mapping for a window on this screen: native = origin + (logical - origin) * ratio.
        const QPointF origin = screen.geometry.topLeft();
        const QPointF nativeTopLeft = origin + (QPointF(geometry.topLeft()) - origin) * screen.ratio;
        const QSizeF nativeSize = QSizeF(geometry.size()) * screen.ratio;
        QVERIFY(nativeTopLeft.x() <= device.left() && nativeTopLeft.y() <= device.top());
        QVERIFY(nativeTopLeft.x() + nativeSize.width() >= device.left() + device.width());
        QVERIFY(nativeTopLeft.y() + nativeSize.height() >= device.top() + device.height());
    }

    void selectionToDevice_data()
    {
        QTest::addColumn<QRect>("selection");
        QTest::addColumn<qreal>("ratio");
        QTest::addColumn<QSize>("bounds");
        QTest::addColumn<QRect>("expected");

        QTest::newRow("ratio 1") << QRect(10, 20, 30, 40) << qreal(1) << QSize() << QRect(10, 20, 30, 40);
        QTest::newRow("ratio 2") << QRect(10, 20, 30, 40) << qreal(2) << QSize() << QRect(20, 40, 60, 80);
        // Edges rounded to the nearest device pixel (4.5 -> 5, 7.5 -> 8), size scaled and rounded (10.5 -> 11, 13.5 -> 14).
        QTest::newRow("ratio 1.5") << QRect(3, 5, 7, 9) << qreal(1.5) << QSize() << QRect(5, 8, 11, 14);
        // 1.25 -> 1, 2.5 -> 3; 3.75 -> 4 both ways.
        QTest::newRow("ratio 1.25") << QRect(1, 2, 3, 3) << qreal(1.25) << QSize() << QRect(1, 3, 4, 4);
        // A typed 100x100 at 125% keeps its size wherever it starts (with "every touched pixel" it was 125x126).
        QTest::newRow("ratio 1.25, typed 100x100 mid-pixel") << QRect(1, 1, 100, 100) << qreal(1.25) << QSize()
                                                            << QRect(1, 1, 125, 125);
        QTest::newRow("ratio 1.75, whole") << QRect(0, 0, 100, 100) << qreal(1.75) << QSize(175, 175)
                                           << QRect(0, 0, 175, 175);
        QTest::newRow("clipped to the bounds") << QRect(90, 90, 20, 20) << qreal(1.5) << QSize(150, 150)
                                               << QRect(135, 135, 15, 15);
        QTest::newRow("empty") << QRect() << qreal(2) << QSize(100, 100) << QRect(0, 0, 0, 0);
        QTest::newRow("not normalized") << QRect(QPoint(50, 50), QPoint(10, 10)) << qreal(2) << QSize(100, 100)
                                        << QRect(100, 100, -78, -78);
    }

    void selectionToDevice()
    {
        QFETCH(QRect, selection);
        QFETCH(qreal, ratio);
        QFETCH(QSize, bounds);
        QFETCH(QRect, expected);

        QCOMPARE(CaptureGeometry::selectionToDevice(selection, ratio, bounds), expected);
    }

    // A selection across the boundary of two screens, in the coordinates of a whole-desktop window on the 100% screen
    // (ratio 1): the device rectangle is the same pixels on the canvas, whichever screen each part is on.
    void selectionAcrossScreens()
    {
        const Layout layout = sideBySide();
        const QRect device = CaptureGeometry::deviceUnion(layout);
        const QRect window = CaptureGeometry::windowGeometry(layout.at(0), device);
        QCOMPARE(window, device);

        const QRect selection(1800, 100, 400, 300); // 120 pixels on the left screen, 280 on the right one
        const QRect result = CaptureGeometry::selectionToDevice(selection, 1, device.size());
        QCOMPARE(result, QRect(1800, 100, 400, 300));
        QVERIFY(result.intersects(CaptureGeometry::deviceRect(layout.at(0)).translated(-device.topLeft())));
        QVERIFY(result.intersects(CaptureGeometry::deviceRect(layout.at(1)).translated(-device.topLeft())));
    }

    // Bug 45: a typed size is scaled by the ratio of the screen the selection starts on, not the dialog window's.
    void typedSizeToDevice_data()
    {
        QTest::addColumn<QPoint>("device");
        QTest::addColumn<QSize>("expected");

        const Layout layout = sideBySide();
        const QPoint left = CaptureGeometry::deviceRect(layout.at(0)).topLeft();
        const QPoint right = CaptureGeometry::deviceRect(layout.at(1)).topLeft();

        QTest::newRow("on the left screen") << left + QPoint(10, 10)
                                            << QSize(qRound(100 * layout.at(0).ratio), qRound(100 * layout.at(0).ratio));
        QTest::newRow("on the right screen") << right + QPoint(10, 10)
                                             << QSize(qRound(100 * layout.at(1).ratio), qRound(100 * layout.at(1).ratio));
        QTest::newRow("off every screen, fallback 2") << QPoint(-50000, -50000) << QSize(200, 200);
    }

    void typedSizeToDevice()
    {
        QFETCH(QPoint, device);
        QFETCH(QSize, expected);

        const Layout layout = sideBySide();
        QVERIFY(layout.at(0).ratio != layout.at(1).ratio);
        QCOMPARE(CaptureGeometry::typedSizeToDevice(layout, device, QSize(100, 100), 2), expected);
    }
};

QTEST_APPLESS_MAIN(tst_CaptureGeometry)
#include "tst_capturegeometry.moc"
