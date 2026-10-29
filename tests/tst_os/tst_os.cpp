// Safety-net tests for the os helpers (tools/os): they record current behaviour, not intended behaviour.
// Assertions marked "expected to change" document a known bug and flip when it is fixed.
#include <QtTest>
#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QTemporaryDir>

#include <tools/os.h>
#include <tools/taskbar.h>
#include <tools/windowpicker.h>

#ifdef Q_OS_WIN
    #include <qt_windows.h>
    #include <dwmapi.h>
#endif

class tst_Os : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
#ifdef Q_OS_WIN
        // Per-monitor DPI aware, as the app is (Qt's windows platform sets it; the offscreen platform the tests run on
        // does not). Unaware, Windows scales some APIs' sizes and not others on a scaled display, and the capture
        // tests compared sizes that disagree (2026-10-29 run at the owner's scaling).
        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    }

    // getDocumentsPath is never empty. On Linux it is $HOME/Documents, whether or not that directory exists.
    void documentsPathNotEmpty()
    {
        const QString path = os::getDocumentsPath();
        QVERIFY(!path.isEmpty());
        QVERIFY(QDir::isAbsolutePath(path));
#ifndef Q_OS_WIN
        QCOMPARE(path, QDir::homePath() + QDir::separator() + "Documents");
#endif
    }

#ifndef Q_OS_WIN
    void documentsPathFollowsHome()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QByteArray oldHome = qgetenv("HOME");
        qputenv("HOME", QFile::encodeName(home.path()));
        const QString path = os::getDocumentsPath();
        qputenv("HOME", oldHome);

        QCOMPARE(path, home.path() + "/Documents");
        QVERIFY(!QDir(path).exists()); // not created
    }
#endif

    // The success sound ships in the resources (M06), so it plays whatever the working directory is.
    void screenshotSoundResource()
    {
        QFile wave(":/sounds/ls.screenshot.wav");
        QVERIFY(wave.open(QIODevice::ReadOnly));
        const QByteArray bytes = wave.readAll();
        QVERIFY(bytes.size() > 44); // more than a bare WAV header
        QVERIFY(bytes.startsWith("RIFF"));
        QCOMPARE(bytes.mid(8, 4), QByteArray("WAVE"));
    }

    // playSound returns at once (asynchronous on Windows, a no-op elsewhere).
    void playSoundReturns()
    {
        os::playSound(os::Sound::Screenshot);
        os::playSound(os::Sound::Error);
    }

    // Without a window the taskbar does nothing (and on Linux it is always a no-op).
    void taskbarWithoutWindow()
    {
        Taskbar taskbar;
        taskbar.setOverlayIcon(os::icon("yes"));
        taskbar.setProgressState(Taskbar::Progress::Paused);
        taskbar.setProgressValue(50);
        taskbar.setProgressState(Taskbar::Progress::Normal);
        taskbar.clearOverlayIcon();
        taskbar.setProgressState(Taskbar::Progress::None);
    }

#ifdef Q_OS_WIN
    // M07: the HBITMAP/HICON conversions that replaced QtWin keep size and pixels.
    void pixmapFromHBITMAP()
    {
        const quint32 pixels[4] = { 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0x80404040 }; // premultiplied ARGB
        HBITMAP bitmap = ::CreateBitmap(2, 2, 1, 32, pixels);
        QVERIFY(bitmap);

        const QImage opaque = os::pixmapFromHBITMAP(bitmap).toImage();
        const QImage alpha = os::pixmapFromHBITMAP(bitmap, true).toImage();
        ::DeleteObject(bitmap);

        QCOMPARE(opaque.size(), QSize(2, 2));
        QVERIFY(!opaque.hasAlphaChannel());
        QCOMPARE(opaque.pixel(0, 0), qRgb(255, 0, 0));
        QCOMPARE(opaque.pixel(1, 1) & 0xFFFFFF, 0x404040u);

        QCOMPARE(alpha.size(), QSize(2, 2));
        QVERIFY(alpha.hasAlphaChannel());
        QCOMPARE(qAlpha(alpha.pixel(0, 1)), 255);
        QCOMPARE(qAlpha(alpha.pixel(1, 1)), 0x80);
    }

    void pixmapFromHICON()
    {
        HICON icon = ::LoadIcon(nullptr, IDI_APPLICATION);
        QVERIFY(icon);

        const QPixmap pixmap = os::pixmapFromHICON(icon);
        QVERIFY(!pixmap.isNull());
        QCOMPARE(pixmap.width(), ::GetSystemMetrics(SM_CXICON));
    }

    // Bug 35: window capture has Windows draw the window itself (PrintWindow), so a window partly off screen comes out
    // whole, its own content included where it is off screen, cropped to the visible frame DWM reports. A plain GDI
    // window cannot show the DirectComposition case (that is the owner's check); this guards the size, the crop and
    // the off-screen part.
    void grabWindowPartlyOffScreen()
    {
        const COLORREF colour = RGB(200, 30, 40);
        HBRUSH brush = ::CreateSolidBrush(colour);

        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = ::DefWindowProcW;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.hbrBackground = brush;
        windowClass.lpszClassName = L"LightscreenRdCaptureTest";
        QVERIFY(::RegisterClassW(&windowClass));

        // 120 pixels of it left of the virtual desktop.
        const int left = ::GetSystemMetrics(SM_XVIRTUALSCREEN) - 120;
        const int top = ::GetSystemMetrics(SM_YVIRTUALSCREEN) + 40;
        HWND window = ::CreateWindowExW(0, windowClass.lpszClassName, L"capture test", WS_OVERLAPPEDWINDOW,
                                        left, top, 400, 300, nullptr, nullptr, windowClass.hInstance, nullptr);
        QVERIFY(window);
        ::ShowWindow(window, SW_SHOWNOACTIVATE);
        ::UpdateWindow(window);
        QTest::qWait(300);

        RECT frame;
        if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
            ::GetWindowRect(window, &frame);
        }

        RECT client;
        ::GetClientRect(window, &client);
        POINT clientOrigin = { 0, 0 };
        ::ClientToScreen(window, &clientOrigin);

        const QImage image = os::grabWindow(WId(window)).toImage();

        ::DestroyWindow(window);
        ::UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
        ::DeleteObject(brush);

        if (image.isNull()) {
            QSKIP("Nothing was rendered (no interactive desktop on this runner?)");
        }

        QCOMPARE(image.size(), QSize(frame.right - frame.left, frame.bottom - frame.top));

        // Inside the client area: the middle, and a point in the part that is off screen.
        const int clientX = clientOrigin.x - frame.left;
        const int clientY = clientOrigin.y - frame.top;
        const QPoint middle(clientX + client.right / 2, clientY + client.bottom / 2);
        const QPoint offScreen(clientX + 20, clientY + client.bottom / 2);
        QVERIFY(offScreen.x() + frame.left < ::GetSystemMetrics(SM_XVIRTUALSCREEN));

        const QRgb expected = qRgb(GetRValue(colour), GetGValue(colour), GetBValue(colour));
        QCOMPARE(image.pixel(middle) & 0xFFFFFF, expected & 0xFFFFFF);
        QCOMPARE(image.pixel(offScreen) & 0xFFFFFF, expected & 0xFFFFFF);
    }

    // Bug 42: the window picker picks the window under the cursor, in physical pixels. The event's position is in
    // Qt's device-independent pixels; the test hands the picker half the cursor's position (what 200% scaling would
    // give) and expects the window under the real cursor, not the one at that other point.
    void windowPickerPicksWindowUnderCursor()
    {
        const COLORREF colour = RGB(30, 160, 60);
        HBRUSH brush = ::CreateSolidBrush(colour);

        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = ::DefWindowProcW;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.hbrBackground = brush;
        windowClass.lpszClassName = L"LightscreenRdPickerTest";
        QVERIFY(::RegisterClassW(&windowClass));

        const int left = ::GetSystemMetrics(SM_CXSCREEN) / 2;
        const int top = ::GetSystemMetrics(SM_CYSCREEN) / 2;
        HWND window = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"picker test",
                                        WS_POPUP, left, top, 300, 200, nullptr, nullptr, windowClass.hInstance, nullptr);
        QVERIFY(window);
        ::ShowWindow(window, SW_SHOWNOACTIVATE);
        ::UpdateWindow(window);
        QTest::qWait(300);

        const POINT target = { left + 150, top + 100 };
        POINT cursor = { 0, 0 };
        const bool ready = ::SetCursorPos(target.x, target.y) && ::GetCursorPos(&cursor) && cursor.x == target.x
                           && cursor.y == target.y && ::GetAncestor(::WindowFromPoint(target), GA_ROOT) == window;

        QPixmap picked;
        int emitted = 0;

        if (ready) {
            WindowPicker *picker = new WindowPicker;
            QObject::connect(picker, &WindowPicker::pixmap, picker, [&](const QPixmap &pixmap) {
                picked = pixmap;
                ++emitted;
            });

            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(0, 0), QPointF(target.x / 2, target.y / 2),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(picker, &release);
            QTest::qWait(100); // the picker deletes itself on close
        }

        ::DestroyWindow(window);
        ::UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
        ::DeleteObject(brush);

        if (!ready) {
            QSKIP("Cannot place the cursor over a test window (no interactive desktop?)");
        }

        QCOMPARE(emitted, 1);
        QVERIFY(!picked.isNull());
        const QImage image = picked.toImage();
        const QRgb expected = qRgb(GetRValue(colour), GetGValue(colour), GetBValue(colour));
        QCOMPARE(image.pixel(image.width() / 2, image.height() / 2) & 0xFFFFFF, expected & 0xFFFFFF);
    }
#endif

    // icon() picks the normal set when the background's HSV value is above 125, the inverted set otherwise.
    void iconByBackground_data()
    {
        QTest::addColumn<QColor>("background");
        QTest::addColumn<QString>("expectedPrefix");

        QTest::newRow("white") << QColor(Qt::white) << ":/icons/";
        QTest::newRow("value 126") << QColor(126, 0, 0) << ":/icons/";
        QTest::newRow("value 125") << QColor(125, 125, 125) << ":/icons/inv/";
        QTest::newRow("black") << QColor(Qt::black) << ":/icons/inv/";
        QTest::newRow("saturated blue (value 255)") << QColor(0, 0, 255) << ":/icons/";
    }

    void iconByBackground()
    {
        QFETCH(QColor, background);
        QFETCH(QString, expectedPrefix);

        const QString otherPrefix = expectedPrefix == ":/icons/" ? ":/icons/inv/" : ":/icons/";
        const QIcon icon = os::icon("yes", background);
        QVERIFY(iconIs(icon, expectedPrefix + "yes"));
        QVERIFY(!iconIs(icon, otherPrefix + "yes"));
    }

    // Without a colour, icon() uses the application palette's Button colour.
    void iconFollowsPalette()
    {
        const QPalette oldPalette = qApp->palette();

        QPalette light = oldPalette;
        light.setColor(QPalette::Button, QColor(200, 200, 200));
        qApp->setPalette(light);
        QVERIFY(iconIs(os::icon("configure"), ":/icons/configure"));

        QPalette dark = oldPalette;
        dark.setColor(QPalette::Button, QColor(40, 40, 40));
        qApp->setPalette(dark);
        QVERIFY(iconIs(os::icon("configure"), ":/icons/inv/configure"));

        // Only Button counts: a dark Window with a light Button still gives the normal set.
        QPalette mixed = oldPalette;
        mixed.setColor(QPalette::Window, Qt::black);
        mixed.setColor(QPalette::Button, Qt::white);
        qApp->setPalette(mixed);
        QVERIFY(iconIs(os::icon("configure"), ":/icons/configure"));

        qApp->setPalette(oldPalette);
    }

    // The four lightscreen.* icons have no inverted variant: on a dark background icon() returns an icon with no
    // pixmap. None of them is requested through icon() today.
    void iconWithoutInvertedVariant()
    {
        QVERIFY(!QImage(":/icons/lightscreen.small").isNull());
        QVERIFY(QImage(":/icons/inv/lightscreen.small").isNull());

        QVERIFY(!os::icon("lightscreen.small", Qt::white).pixmap(16, 16).isNull());
        QVERIFY(os::icon("lightscreen.small", Qt::black).pixmap(16, 16).isNull());
    }

    // Each inverted icon is the same picture as its normal one, light on dark: same size and the same transparency
    // mask. Bug 31: until 3.0 the inverted "upload" was the old Imgur logo and "configure" a wrench, which Qt 6
    // shows on Windows in dark mode.
    void invertedIconsMatchTheNormalSet()
    {
        const QStringList names = QDir(":/icons/inv").entryList(QDir::Files);
        QVERIFY(names.size() >= 20);

        for (const QString &name : names) {
            const QImage normal = QImage(":/icons/" + name).convertToFormat(QImage::Format_Alpha8);
            const QImage inverted = QImage(":/icons/inv/" + name).convertToFormat(QImage::Format_Alpha8);

            QVERIFY2(!normal.isNull(), qPrintable(name + " has no normal icon"));
            QVERIFY2(normal.size() == inverted.size(), qPrintable(name + ": sizes differ"));
            QVERIFY2(normal == inverted, qPrintable(name + ": the shapes differ"));
        }
    }

    // An unknown name gives an icon with no pixmap rather than a warning or a fallback.
    void iconUnknownName()
    {
        QVERIFY(os::icon("no-such-icon", Qt::white).pixmap(16, 16).isNull());
        QVERIFY(os::icon("no-such-icon", Qt::black).pixmap(16, 16).isNull());
    }

#if defined(Q_OS_LINUX)
    // setStartup writes $HOME/.config/autostart/lightscreen.desktop. HOME points at a temporary directory here.
    void startupWritesDesktopFile()
    {
        QVERIFY(QDir(mHome->path()).mkpath(".config/autostart"));

        os::setStartup(true, false);

        QCOMPARE(readDesktopFile(), expectedDesktopFile(false));
    }

    void startupHideAddsFlag()
    {
        QVERIFY(QDir(mHome->path()).mkpath(".config/autostart"));

        os::setStartup(true, true);

        QCOMPARE(readDesktopFile(), expectedDesktopFile(true));
    }

    // An existing file (ours or the user's) is replaced, not appended to.
    void startupReplacesExistingFile()
    {
        QVERIFY(QDir(mHome->path()).mkpath(".config/autostart"));
        QFile old(desktopFilePath());
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("[Desktop Entry]\nExec=something-else\nName=Old\nType=Application\n");
        old.close();

        os::setStartup(true, false);

        QCOMPARE(readDesktopFile(), expectedDesktopFile(false));
    }

    void startupFalseRemovesDesktopFile()
    {
        QVERIFY(QDir(mHome->path()).mkpath(".config/autostart"));
        os::setStartup(true, false);
        QVERIFY(QFile::exists(desktopFilePath()));

        os::setStartup(false, false);
        QVERIFY(!QFile::exists(desktopFilePath()));
        QVERIFY(QDir(mHome->path() + "/.config/autostart").exists()); // the directory stays

        // Turning it off again with no file is harmless.
        os::setStartup(false, true);
        QVERIFY(!QFile::exists(desktopFilePath()));
    }

    // With no ~/.config/autostart directory, setStartup(true) creates it and writes the file (bug 25).
    void startupWithoutAutostartDir()
    {
        QVERIFY(!QDir(mHome->path() + "/.config").exists());

        os::setStartup(true, false);

        QVERIFY(QDir(mHome->path() + "/.config/autostart").exists());
        QCOMPARE(readDesktopFile(), expectedDesktopFile(false));
    }

    // When the directory cannot be created (here a plain file is in the way), setStartup warns instead of
    // failing silently.
    void startupUnwritableWarns()
    {
        QVERIFY(QDir(mHome->path()).mkpath(".config"));
        QFile blocker(mHome->path() + "/.config/autostart");
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Could not write .*lightscreen\\.desktop"));
        os::setStartup(true, false);

        QVERIFY(!QFile::exists(desktopFilePath()));
    }

    // The Exec value quotes the program per the Desktop Entry spec: spaces survive, and ", `, $ and \ are
    // escaped inside the quotes (\ twice, once more for the string-value escape). % is doubled.
    void desktopExec_data()
    {
        QTest::addColumn<QString>("program");
        QTest::addColumn<bool>("hide");
        QTest::addColumn<QString>("exec");

        QTest::newRow("plain") << "/usr/bin/lightscreen" << false << "\"/usr/bin/lightscreen\"";
        QTest::newRow("hide") << "/usr/bin/lightscreen" << true << "\"/usr/bin/lightscreen\" -h";
        QTest::newRow("space") << "/home/a b/lightscreen" << false << "\"/home/a b/lightscreen\"";
        QTest::newRow("reserved") << "/x/\"`$/ls" << false << "\"/x/\\\\\"\\\\`\\\\$/ls\"";
        QTest::newRow("backslash") << "/x\\y/ls" << false << "\"/x\\\\\\\\y/ls\"";
        QTest::newRow("percent") << "/x/100%/ls" << false << "\"/x/100%%/ls\"";
    }

    void desktopExec()
    {
        QFETCH(QString, program);
        QFETCH(bool, hide);
        QFETCH(QString, exec);

        QCOMPARE(os::desktopExec(program, hide), exec);
    }

    void init()
    {
        mHome = new QTemporaryDir;
        QVERIFY(mHome->isValid());
        mOldHome = qgetenv("HOME");
        qputenv("HOME", QFile::encodeName(mHome->path()));
        QCOMPARE(QDir::homePath(), mHome->path());
    }

    void cleanup()
    {
        if (mHome) {
            qputenv("HOME", mOldHome);
            delete mHome;
            mHome = nullptr;
        }
    }
#endif

private:
    // True when the icon's pixmap is the image stored under resourcePath. Compared premultiplied, because the
    // gray+alpha PNGs come back from QIcon with different colour values in fully transparent pixels.
    static bool iconIs(const QIcon &icon, const QString &resourcePath)
    {
        const QImage expected = QImage(resourcePath).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (expected.isNull()) {
            qWarning("No resource %s", qPrintable(resourcePath));
            return false;
        }

        const QImage actual =
            icon.pixmap(expected.size()).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        return actual == expected;
    }

#if defined(Q_OS_LINUX)
    QString desktopFilePath() const
    {
        return mHome->path() + "/.config/autostart/lightscreen.desktop";
    }

    QString readDesktopFile() const
    {
        QFile file(desktopFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    }

    static QString expectedDesktopFile(bool hide)
    {
        return "[Desktop Entry]\nType=Application\nName=Lightscreen\nExec=" + os::desktopExec(appPath(), hide) + "\n";
    }

    static QString appPath()
    {
        return QDir::toNativeSeparators(qApp->applicationFilePath());
    }

    QTemporaryDir *mHome = nullptr;
    QByteArray mOldHome;
#endif
};

QTEST_MAIN(tst_Os)
#include "tst_os.moc"
