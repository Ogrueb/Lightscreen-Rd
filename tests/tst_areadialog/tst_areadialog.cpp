// Safety net for dialogs/areadialog: the geometry behind area selection (resultRect, keyboard sizing,
// limitPointToRect and the handle positions). The dialog is never shown, so its size is whatever the test resizes
// it to and no desktop grab happens.
// These tests record current behaviour, quirks included, not intended behaviour (docs/modernization-plan.md, section 6).
// S01 changed the constructor; S04 replaced the resultRect mapping and flipped the fractional rows (bug 23).
#include <QtTest>
#include <QKeyEvent>
#include <QSettings>
#include <QTemporaryDir>

#include <dialogs/areadialog.h>
#include <tools/screenshot.h>
#include <tools/screenshotmanager.h>
#include <tools/uploader/uploader.h>

// Opens the protected members the tests need.
class TestAreaDialog : public AreaDialog
{
public:
    using AreaDialog::AreaDialog;
    using AreaDialog::keyboardResize;
    using AreaDialog::keyPressEvent;
    using AreaDialog::limitPointToRect;
    using AreaDialog::updateHandles;
    using AreaDialog::mSelection;
    using AreaDialog::mKeyboardSize;
    using AreaDialog::mTLHandle;
    using AreaDialog::mTRHandle;
    using AreaDialog::mBLHandle;
    using AreaDialog::mBRHandle;
    using AreaDialog::mLHandle;
    using AreaDialog::mTHandle;
    using AreaDialog::mRHandle;
    using AreaDialog::mBHandle;
};

typedef QVector<QRect> RectList;
Q_DECLARE_METATYPE(RectList)

class tst_AreaDialog : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir *mDir = nullptr;
    QSettings *mSettings = nullptr;
    ScreenshotManager *mManager = nullptr;
    Screenshot *mScreenshot = nullptr;

    static Screenshot::Options options()
    {
        Screenshot::Options options;
        options.result = Screenshot::Failure;
        options.format = Screenshot::PNG;
        options.namingOptions.naming = Screenshot::Empty;
        options.namingOptions.flip = false;
        options.namingOptions.leadingZeros = 0;
        options.prefix = QStringLiteral("shot");
        options.mode = Screenshot::SelectedArea;
        options.quality = 90;
        options.animations = false;
        options.clipboard = false;
        options.urlClipboard = false;
        options.currentMonitor = false;
        options.cursor = false;
        options.file = false;
        options.magnify = false;
        options.optimize = false;
        options.preview = false;
        options.replace = false;
        options.saveAs = false;
        options.upload = false;
        return options;
    }

    // Gives the screenshot a pixmap of the given logical size and device pixel ratio, without the confirmation
    // path that setPixmap() takes.
    void setPixmap(QSize logicalSize, qreal ratio)
    {
        QPixmap pixmap(logicalSize * ratio);
        pixmap.fill(Qt::gray);
        pixmap.setDevicePixelRatio(ratio);
        mScreenshot->pixmap() = pixmap;
    }

    // A dialog of 640x480 with the selection at (10, 20) 30x40.
    TestAreaDialog *dialog()
    {
        auto d = new TestAreaDialog(mScreenshot->pixmap(), mScreenshot->options());
        d->resize(640, 480);
        d->mSelection = QRect(10, 20, 30, 40);
        return d;
    }

    // Sends one key press per character, as typing would.
    static void type(TestAreaDialog *d, const QString &text)
    {
        for (const QChar &c : text) {
            int key = c.toUpper().unicode();
            Qt::KeyboardModifiers modifiers = c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier;
            QKeyEvent event(QEvent::KeyPress, key, modifiers, QString(c));
            d->keyPressEvent(&event);
        }
    }

    static void press(TestAreaDialog *d, int key)
    {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        d->keyPressEvent(&event);
    }

    static RectList handles(TestAreaDialog *d)
    {
        return RectList() << d->mTLHandle << d->mTRHandle << d->mBLHandle << d->mBRHandle
                          << d->mLHandle << d->mTHandle << d->mRHandle << d->mBHandle;
    }

    static QString describe(const RectList &rects)
    {
        QStringList parts;
        for (const QRect &r : rects) {
            parts << QString("(%1,%2 %3x%4)").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        }
        return parts.join(' ');
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);

        mDir = new QTemporaryDir;
        QVERIFY(mDir->isValid());

        // The constructor reads options/areaAutoclose from the manager's settings; it is unset, so false.
        mSettings = new QSettings(mDir->filePath("settings.ini"), QSettings::IniFormat);
        mManager = new ScreenshotManager(mSettings, mDir->path() + QDir::separator());
        ScreenshotManager::setInstance(mManager);

        mScreenshot = new Screenshot(nullptr, options());
    }

    void cleanupTestCase()
    {
        delete mScreenshot;
        ScreenshotManager::setInstance(nullptr);
        delete mManager;
        delete mSettings;
        delete mDir;
    }

    void init()
    {
        setPixmap(QSize(640, 480), 1);
    }

    // resultRect: the selection times the pixmap's device pixel ratio, edges and size each rounded to the nearest
    // device pixel, clipped to the pixmap. Until S04 each value was truncated (bug 23); from S04 to the 3.0 QA pass the
    // near edges were rounded down and the far edges up, which made a typed size one pixel too large mid-pixel.

    void resultRect_data()
    {
        QTest::addColumn<qreal>("ratio");
        QTest::addColumn<QRect>("selection");
        QTest::addColumn<QRect>("expected");

        QTest::newRow("ratio 1") << qreal(1) << QRect(10, 20, 300, 200) << QRect(10, 20, 300, 200);
        QTest::newRow("ratio 2") << qreal(2) << QRect(10, 20, 300, 200) << QRect(20, 40, 600, 400);
        QTest::newRow("ratio 2, origin") << qreal(2) << QRect(0, 0, 640, 480) << QRect(0, 0, 1280, 960);
        // 10.5 and 13.5 device pixels round to 11 and 14 (bug 23; until S04 these rows were "truncates",
        // QRect(4, 7, 10, 13) and QRect(1, 1, 3, 3); until the 3.0 QA pass the first was QRect(4, 7, 11, 14)).
        QTest::newRow("ratio 1.5 rounds") << qreal(1.5) << QRect(3, 5, 7, 9) << QRect(5, 8, 11, 14);
        QTest::newRow("ratio 1.25 rounds") << qreal(1.25) << QRect(1, 1, 3, 3) << QRect(1, 1, 4, 4);
        QTest::newRow("ratio 1.25, typed 100x100 keeps its size") << qreal(1.25) << QRect(1, 1, 100, 100)
                                                                  << QRect(1, 1, 125, 125);
        QTest::newRow("ratio 1.75, far edge") << qreal(1.75) << QRect(0, 0, 640, 480) << QRect(0, 0, 1120, 840);
        // New in S04: a selection past the pixmap's edge is clipped to it.
        QTest::newRow("ratio 1, clipped") << qreal(1) << QRect(600, 400, 100, 100) << QRect(600, 400, 40, 80);
        QTest::newRow("ratio 2, empty selection") << qreal(2) << QRect() << QRect(0, 0, 0, 0);
        // The selection is not normalized first: a negative size stays negative.
        QTest::newRow("ratio 2, not normalized") << qreal(2) << QRect(QPoint(50, 50), QPoint(10, 10))
                                                 << QRect(100, 100, -78, -78);
    }

    void resultRect()
    {
        QFETCH(qreal, ratio);
        QFETCH(QRect, selection);
        QFETCH(QRect, expected);

        setPixmap(QSize(640, 480), ratio);
        QScopedPointer<TestAreaDialog> d(dialog());
        d->mSelection = selection;

        QCOMPARE(d->resultRect(), expected);
    }

    // Bug 45: a typed size is meant at the ratio of the screen the selection is on. The dialog window here has ratio
    // 1.5 (as on a 150% main screen) while the only screen has ratio 1 (the selection on a 100% screen): typed 100x100
    // saves 100x100, not 150x150. On one screen with the window's own ratio nothing changes (keyboardResize above).
    void keyboardResize_usesTheSelectionsScreenRatio()
    {
        QCOMPARE(QGuiApplication::primaryScreen()->devicePixelRatio(), qreal(1));
        setPixmap(QSize(640, 480), 1.5);
        QScopedPointer<TestAreaDialog> d(dialog());
        d->mKeyboardSize = "100x100";
        d->keyboardResize();

        QCOMPARE(d->mSelection.size(), QSize(67, 67));
        QCOMPARE(d->resultRect().size(), QSize(100, 100));
    }

    // A null pixmap has ratio 1.
    void resultRect_nullPixmap()
    {
        mScreenshot->pixmap() = QPixmap();
        QScopedPointer<TestAreaDialog> d(dialog());
        d->mSelection = QRect(7, 8, 9, 10);

        QCOMPARE(d->resultRect(), QRect(7, 8, 9, 10));
    }

    // keyboardResize: parses mKeyboardSize as <width>x<height> and resizes the selection in place (top left kept),
    // but only when the result still fits inside the dialog. mKeyboardSize is cleared either way.

    void keyboardResize_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QRect>("expected");

        const QRect unchanged(10, 20, 30, 40);

        QTest::newRow("100x100") << "100x100" << QRect(10, 20, 100, 100);
        QTest::newRow("leading zeros") << "0100x050" << QRect(10, 20, 100, 50);
        QTest::newRow("0x10") << "0x10" << unchanged;
        QTest::newRow("10x0") << "10x0" << unchanged;
        QTest::newRow("abc") << "abc" << unchanged;
        QTest::newRow("axb") << "axb" << unchanged;
        QTest::newRow("100x") << "100x" << unchanged;
        QTest::newRow("x100") << "x100" << unchanged;
        QTest::newRow("1x2x3") << "1x2x3" << unchanged;
        QTest::newRow("empty") << "" << unchanged;
        // Not reachable by typing since bug 24 (keyPressEvent stores the X key as a lower-case x).
        QTest::newRow("upper case X") << "100X100" << unchanged;
        QTest::newRow("negative") << "-5x10" << unchanged;
        // Not reachable by typing (keyPressEvent only lets digits and x through), but QString::toInt accepts them.
        QTest::newRow("plus sign") << "+100x+50" << QRect(10, 20, 100, 50);
        QTest::newRow("spaces") << " 100 x 50 " << QRect(10, 20, 100, 50);
        // The dialog is 640x480 and the selection starts at (10, 20): 630x460 reaches the edges exactly.
        QTest::newRow("fits exactly") << "630x460" << QRect(10, 20, 630, 460);
        QTest::newRow("one too wide") << "631x460" << unchanged;
        QTest::newRow("one too high") << "630x461" << unchanged;
    }

    void keyboardResize()
    {
        QFETCH(QString, input);
        QFETCH(QRect, expected);

        QScopedPointer<TestAreaDialog> d(dialog());
        d->mKeyboardSize = input;
        d->keyboardResize();

        QCOMPARE(d->mSelection, expected);
        QVERIFY(d->mKeyboardSize.isEmpty());
    }

    // With no selection yet, the size is applied from the dialog's top left corner.
    void keyboardResize_noSelection()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        d->mSelection = QRect();
        d->mKeyboardSize = "100x50";
        d->keyboardResize();

        QCOMPARE(d->mSelection, QRect(0, 0, 100, 50));
    }

    // A successful resize moves the handles; a rejected one does not.
    void keyboardResize_updatesHandles()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        d->mKeyboardSize = "200x100";
        d->keyboardResize();
        QCOMPARE(d->mBRHandle, QRect(199, 109, 10, 10));

        d->mKeyboardSize = "900x100";
        d->keyboardResize();
        QCOMPARE(d->mSelection, QRect(10, 20, 200, 100));
        QCOMPARE(d->mBRHandle, QRect(199, 109, 10, 10));
    }

    // Typing: digits and x are collected, Return applies them.

    void typing_sizeAndReturn()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        type(d.data(), "100x100");
        QCOMPARE(d->mKeyboardSize, QString("100x100"));

        press(d.data(), Qt::Key_Return);
        QCOMPARE(d->mSelection, QRect(10, 20, 100, 100));
        QVERIFY(d->mKeyboardSize.isEmpty());
        QCOMPARE(d->result(), int(QDialog::Rejected)); // Return applied the size, it did not accept the dialog
    }

    void typing_enterKey()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        type(d.data(), "50x60");
        press(d.data(), Qt::Key_Enter);
        QCOMPARE(d->mSelection, QRect(10, 20, 50, 60));
    }

    // Letters other than x are ignored (and the event is ignored, so it propagates).
    void typing_lettersIgnored()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        QKeyEvent event(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
        d->keyPressEvent(&event);
        QVERIFY(!event.isAccepted());

        type(d.data(), "abc");
        QVERIFY(d->mKeyboardSize.isEmpty());
    }

    // A second x is dropped, so "1x2x3" becomes "1x23".
    void typing_secondXDropped()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        type(d.data(), "1x2x3");
        QCOMPARE(d->mKeyboardSize, QString("1x23"));
    }

    // Shift+X or Caps Lock types an upper-case X; it is stored as a lower-case x, so Return applies the size, and a
    // second x of either case is refused (bug 24).
    void typing_upperCaseX()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        type(d.data(), "100X50");
        QCOMPARE(d->mKeyboardSize, QString("100x50"));

        press(d.data(), Qt::Key_Return);
        QCOMPARE(d->mSelection, QRect(10, 20, 100, 50));
        QVERIFY(d->mKeyboardSize.isEmpty());

        type(d.data(), "1X2x3X4");
        QCOMPARE(d->mKeyboardSize, QString("1x234"));
    }

    void typing_backspace()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        type(d.data(), "100x");
        press(d.data(), Qt::Key_Backspace);
        QCOMPARE(d->mKeyboardSize, QString("100"));

        type(d.data(), "x80");
        press(d.data(), Qt::Key_Return);
        QCOMPARE(d->mSelection, QRect(10, 20, 100, 80));
    }

    // Escape with a pending size clears it and keeps the dialog open; with none it rejects.
    void typing_escape()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        d->setResult(-1);
        type(d.data(), "12");
        press(d.data(), Qt::Key_Escape);
        QVERIFY(d->mKeyboardSize.isEmpty());
        QCOMPARE(d->result(), -1);

        press(d.data(), Qt::Key_Escape);
        QCOMPARE(d->result(), int(QDialog::Rejected));
    }

    // Each digit on its own is compared with the dialog's larger side, so on a dialog smaller than 10 pixels the
    // higher digits cannot be typed. The whole number is not checked here.
    void typing_digitLimit()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        d->resize(5, 4);
        type(d.data(), "9");
        QVERIFY(d->mKeyboardSize.isEmpty());

        type(d.data(), "5");
        QCOMPARE(d->mKeyboardSize, QString("5"));
    }

    // limitPointToRect: clamps to [left, right] x [top, bottom], where right and bottom are QRect's inclusive
    // edges (x + width - 1).

    void limitPointToRect_data()
    {
        QTest::addColumn<QPoint>("point");
        QTest::addColumn<QRect>("rect");
        QTest::addColumn<QPoint>("expected");

        const QRect screen(0, 0, 640, 480);

        QTest::newRow("inside") << QPoint(10, 20) << screen << QPoint(10, 20);
        QTest::newRow("top left corner") << QPoint(0, 0) << screen << QPoint(0, 0);
        QTest::newRow("left and above") << QPoint(-5, -7) << screen << QPoint(0, 0);
        QTest::newRow("bottom right corner") << QPoint(639, 479) << screen << QPoint(639, 479);
        QTest::newRow("width and height") << QPoint(640, 480) << screen << QPoint(639, 479);
        QTest::newRow("right and below") << QPoint(700, 500) << screen << QPoint(639, 479);
        QTest::newRow("only x out") << QPoint(-1, 200) << screen << QPoint(0, 200);
        QTest::newRow("only y out") << QPoint(200, 900) << screen << QPoint(200, 479);

        const QRect offset(-100, -50, 200, 100);
        QTest::newRow("negative offset, left") << QPoint(-150, 0) << offset << QPoint(-100, 0);
        QTest::newRow("negative offset, right and below") << QPoint(150, 60) << offset << QPoint(99, 49);
        QTest::newRow("negative offset, inside") << QPoint(-20, -10) << offset << QPoint(-20, -10);

        // An inverted rectangle (right < left) is not normalized: points left of it go to left, everything
        // else to right, so the result can lie on either side.
        const QRect inverted(QPoint(10, 10), QPoint(0, 0));
        QTest::newRow("inverted, before") << QPoint(-5, -5) << inverted << QPoint(10, 10);
        QTest::newRow("inverted, between") << QPoint(5, 5) << inverted << QPoint(10, 10);
        QTest::newRow("inverted, after") << QPoint(20, 20) << inverted << QPoint(0, 0);

        // A null rectangle (right = left - 1) maps everything to (0, 0) or (-1, -1).
        QTest::newRow("null rect, inside") << QPoint(0, 0) << QRect() << QPoint(-1, -1);
        QTest::newRow("null rect, before") << QPoint(-5, -5) << QRect() << QPoint(0, 0);
    }

    void limitPointToRect()
    {
        QFETCH(QPoint, point);
        QFETCH(QRect, rect);
        QFETCH(QPoint, expected);

        QScopedPointer<TestAreaDialog> d(dialog());
        QCOMPARE(d->limitPointToRect(point, rect), expected);
    }

    // updateHandles: 10x10 handles on the selection shrunk by one pixel at the right and bottom. Order:
    // TL, TR, BL, BR, L, T, R, B.

    void handlesBeforeUpdate()
    {
        QScopedPointer<TestAreaDialog> d(dialog());
        const RectList actual = handles(d.data());

        for (const QRect &r : actual) {
            QCOMPARE(r, QRect(0, 0, 10, 10));
        }
    }

    void updateHandles_data()
    {
        QTest::addColumn<QRect>("selection");
        QTest::addColumn<RectList>("expected");

        QTest::newRow("200x100 at (100, 50)") << QRect(100, 50, 200, 100)
            << (RectList() << QRect(100, 50, 10, 10) << QRect(289, 50, 10, 10)
                           << QRect(100, 139, 10, 10) << QRect(289, 139, 10, 10)
                           << QRect(100, 94, 10, 10) << QRect(194, 50, 10, 10)
                           << QRect(289, 94, 10, 10) << QRect(194, 139, 10, 10));

        QTest::newRow("whole 640x480") << QRect(0, 0, 640, 480)
            << (RectList() << QRect(0, 0, 10, 10) << QRect(629, 0, 10, 10)
                           << QRect(0, 469, 10, 10) << QRect(629, 469, 10, 10)
                           << QRect(0, 234, 10, 10) << QRect(314, 0, 10, 10)
                           << QRect(629, 234, 10, 10) << QRect(314, 469, 10, 10));

        // A selection smaller than a handle: the right and bottom handles end up left of and above it.
        QTest::newRow("5x5 at (0, 0)") << QRect(0, 0, 5, 5)
            << (RectList() << QRect(0, 0, 10, 10) << QRect(-6, 0, 10, 10)
                           << QRect(0, -6, 10, 10) << QRect(-6, -6, 10, 10)
                           << QRect(0, -3, 10, 10) << QRect(-3, 0, 10, 10)
                           << QRect(-6, -3, 10, 10) << QRect(-3, -6, 10, 10));

        // The selection is normalized first, so an inverted one gives the handles of its normalized form.
        QTest::newRow("inverted") << QRect(QPoint(299, 149), QPoint(100, 50))
            << (RectList() << QRect(100, 50, 10, 10) << QRect(289, 50, 10, 10)
                           << QRect(100, 139, 10, 10) << QRect(289, 139, 10, 10)
                           << QRect(100, 94, 10, 10) << QRect(194, 50, 10, 10)
                           << QRect(289, 94, 10, 10) << QRect(194, 139, 10, 10));

        QTest::newRow("no selection") << QRect()
            << (RectList() << QRect(0, 0, 10, 10) << QRect(-11, 0, 10, 10)
                           << QRect(0, -11, 10, 10) << QRect(-11, -11, 10, 10)
                           << QRect(0, -5, 10, 10) << QRect(-5, 0, 10, 10)
                           << QRect(-11, -5, 10, 10) << QRect(-5, -11, 10, 10));
    }

    void updateHandles()
    {
        QFETCH(QRect, selection);
        QFETCH(RectList, expected);

        QScopedPointer<TestAreaDialog> d(dialog());
        d->mSelection = selection;
        d->updateHandles();

        const RectList actual = handles(d.data());
        // std::equal, not QCOMPARE on the vectors: see docs/status.md (Windows CI and QList::operator==).
        QVERIFY2(actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin()),
                 qPrintable(QString("actual %1\nexpected %2").arg(describe(actual), describe(expected))));
    }
};

QTEST_MAIN(tst_AreaDialog)
#include "tst_areadialog.moc"
