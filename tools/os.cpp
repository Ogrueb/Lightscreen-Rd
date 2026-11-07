/*
 * Copyright (C) Christian Kaiser
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
#include <QApplication>
#include <QBitmap>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QDir>
#include <QGraphicsDropShadowEffect>
#include <QIcon>
#include <QLibrary>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QTextEdit>
#include <QTimeLine>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <string>
#include <QMessageBox>
#include <QPainter>
#include <QPair>

#ifdef Q_OS_WIN
    #include <qt_windows.h>
    #include <ShlObj.h>
    #include <dwmapi.h>
    #include <mmsystem.h>

    // PrintWindow's "render DirectComposition content too" flag, Windows 8.1 and later; older SDK headers lack it.
    #ifndef PW_RENDERFULLCONTENT
        #define PW_RENDERFULLCONTENT 0x00000002
    #endif

    // Define for MinGW
    #ifndef SM_CXPADDEDBORDER
        #define SM_CXPADDEDBORDER 92
    #endif
#elif defined(Q_OS_LINUX)
    #include <X11/X.h>
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xatom.h>
#endif

#include <tools/hdrcapture.h>
#include <tools/os.h>

#ifdef Q_OS_WIN
QPixmap os::pixmapFromHBITMAP(HBITMAP bitmap, bool alpha)
{
    if (!alpha) {
        return QPixmap::fromImage(QImage::fromHBITMAP(bitmap));
    }

    // QtWin::HBitmapAlpha read the bitmap as premultiplied ARGB. QImage::fromHBITMAP makes every pixel
    // opaque, so read the 32-bit pixels, alpha included, with GetDIBits.
    BITMAP info;

    if (!::GetObject(bitmap, sizeof(info), &info)) {
        return QPixmap();
    }

    QImage image(info.bmWidth, info.bmHeight, QImage::Format_ARGB32_Premultiplied);

    BITMAPINFO bitmapInfo;
    ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = info.bmWidth;
    bitmapInfo.bmiHeader.biHeight = -info.bmHeight; // top-down rows, as in QImage
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC dc = ::GetDC(nullptr);
    const int lines = ::GetDIBits(dc, bitmap, 0, info.bmHeight, image.bits(), &bitmapInfo, DIB_RGB_COLORS);
    ::ReleaseDC(nullptr, dc);

    if (lines != info.bmHeight) {
        return QPixmap();
    }

    return QPixmap::fromImage(image);
}

QPixmap os::pixmapFromHICON(HICON icon)
{
    return QPixmap::fromImage(QImage::fromHICON(icon));
}
#endif

QPair<QPixmap, QPoint> os::cursor()
{
#ifdef Q_OS_WIN
    /*
    * Taken from: git://github.com/arrai/mumble-record.git > src > mumble > Overlay.cpp
    * BSD License.
    */

    QPixmap pixmap;
    QPoint hotspot;

    CURSORINFO cursorInfo;
    cursorInfo.cbSize = sizeof(cursorInfo);
    ::GetCursorInfo(&cursorInfo);

    HICON cursor = cursorInfo.hCursor;

    ICONINFO iconInfo;
    ::GetIconInfo(cursor, &iconInfo);

    ICONINFO info;
    ZeroMemory(&info, sizeof(info));

    if (::GetIconInfo(cursor, &info)) {
        if (info.hbmColor) {
            pixmap = os::pixmapFromHBITMAP(info.hbmColor, true);
        } else {
            QBitmap orig = QBitmap::fromPixmap(os::pixmapFromHBITMAP(info.hbmMask));
            QImage img = orig.toImage();

            int h = img.height() / 2;
            int w = static_cast<uint>(img.bytesPerLine()) / sizeof(quint32);

            QImage out(img.width(), h, QImage::Format_MonoLSB);
            QImage outmask(img.width(), h, QImage::Format_MonoLSB);

            for (int i = 0; i < h; ++i) {
                const quint32 *srcimg = reinterpret_cast<const quint32 *>(img.scanLine(i + h));
                const quint32 *srcmask = reinterpret_cast<const quint32 *>(img.scanLine(i));

                quint32 *dstimg = reinterpret_cast<quint32 *>(out.scanLine(i));
                quint32 *dstmask = reinterpret_cast<quint32 *>(outmask.scanLine(i));

                for (int j = 0; j < w; ++j) {
                    dstmask[j] = srcmask[j];
                    dstimg[j] = srcimg[j];
                }
            }

            pixmap = QBitmap::fromImage(out, Qt::ColorOnly);
        }

        hotspot.setX(static_cast<int>(info.xHotspot));
        hotspot.setY(static_cast<int>(info.yHotspot));

        if (info.hbmMask) {
            ::DeleteObject(info.hbmMask);
        }

        if (info.hbmColor) {
            ::DeleteObject(info.hbmColor);
        }

        ::DeleteObject(cursor);
    }

    return QPair<QPixmap, QPoint>(pixmap, hotspot);
#else
    return QPair<QPixmap, QPoint>(QPixmap(), QPoint());
#endif
}

void os::effect(QObject *target, const char *slot, int frames, int duration, const char *cleanup)
{
    QTimeLine *timeLine = new QTimeLine(duration);
    timeLine->setFrameRange(0, frames);

    timeLine->connect(timeLine, SIGNAL(frameChanged(int)), target, slot);

    if (cleanup != 0) {
        timeLine->connect(timeLine, SIGNAL(finished()), target, SLOT(cleanup()));
    }

    timeLine->connect(timeLine, SIGNAL(finished()), timeLine, SLOT(deleteLater()));


    timeLine->start();
}

QString os::getDocumentsPath()
{
#ifdef Q_OS_WIN
    PWSTR path = nullptr;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, NULL, &path))) {
        const QString documents = QString::fromWCharArray(path);
        CoTaskMemFree(path);
        return documents;
    }

    CoTaskMemFree(path);

    return QDir::homePath() + QDir::separator() + "My Documents";
#else
    return QDir::homePath() + QDir::separator() + "Documents";
#endif
}

QPixmap os::grabWindow(WId winId)
{
#ifdef Q_OS_WIN
    RECT rcWindow;
    RECT rcFull;

    HWND hwndId = (HWND)winId;

    GetWindowRect(hwndId, &rcFull);

    // Since Windows 10 the visible frame sits inside an invisible resize border;
    // DWM reports the visible bounds directly.
    if (FAILED(DwmGetWindowAttribute(hwndId, DWMWA_EXTENDED_FRAME_BOUNDS, &rcWindow, sizeof(rcWindow)))) {
        rcWindow = rcFull;

        int margin = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER) / 2;

        rcWindow.right -= margin;
        rcWindow.left += margin;

        if (IsZoomed(hwndId)) {
            rcWindow.top += margin;
        } else {
            rcWindow.top += GetSystemMetrics(SM_CXPADDEDBORDER);
        }

        rcWindow.bottom -= margin;
    }

    const int width = rcWindow.right - rcWindow.left;
    const int height = rcWindow.bottom - rcWindow.top;
    const int fullWidth = rcFull.right - rcFull.left;
    const int fullHeight = rcFull.bottom - rcFull.top;

    // On a monitor in HDR mode, Windows.Graphics.Capture reads the window in FP16, where PrintWindow can return a
    // browser several times too bright (bug 9). Its frame is the window's visible frame, or the whole window rectangle
    // on some versions, which is cropped as below.
    const QPixmap hdr = HdrCapture::grabWindow(winId);

    if (!hdr.isNull()) {
        if (hdr.size() == QSize(fullWidth, fullHeight)) {
            return hdr.copy(rcWindow.left - rcFull.left, rcWindow.top - rcFull.top, width, height);
        }

        return hdr;
    }

    // PrintWindow with PW_RENDERFULLCONTENT (Windows 8.1 and later) has Windows draw the window itself into our
    // bitmap, DirectComposition content included (the title bars and toolbars of current apps, browsers), whatever
    // covers it and wherever it is. Until 3.0 the window was copied from the screen when it was fully on screen,
    // which caught whatever was on top at that moment (the window picker as it closed, or the window behind), and
    // from its own window DC otherwise, where DirectComposition content is black (QA pass, bug 35).
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmFull = CreateCompatibleBitmap(hdcScreen, fullWidth, fullHeight);
    HGDIOBJ previous = SelectObject(hdcMem, hbmFull);

    if (!PrintWindow(hwndId, hdcMem, PW_RENDERFULLCONTENT)) {
        // Fallback: the window's area as it is on screen.
        BitBlt(hdcMem, 0, 0, fullWidth, fullHeight, hdcScreen, rcFull.left, rcFull.top, SRCCOPY);
    }

    SelectObject(hdcMem, previous);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    const QPixmap full = os::pixmapFromHBITMAP(hbmFull);
    DeleteObject(hbmFull);

    // The bitmap covers GetWindowRect, invisible resize borders included; keep the visible frame.
    return full.copy(rcWindow.left - rcFull.left, rcWindow.top - rcFull.top, width, height);
#else
    return QGuiApplication::primaryScreen()->grabWindow(winId);
#endif
}

void os::playSound(os::Sound sound)
{
#ifdef Q_OS_WIN
    if (sound == os::Sound::Screenshot) {
        // SND_MEMORY with SND_ASYNC reads the bytes while the sound plays, so they must outlive the call.
        static const QByteArray wave = [] {
            QFile file(":/sounds/ls.screenshot.wav");
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        }();

        if (!wave.isEmpty()) {
            PlaySoundW(reinterpret_cast<LPCWSTR>(wave.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        }
    } else {
        // The system default sound, which is what the old QSound::play of a missing file fell back to.
        PlaySoundW(reinterpret_cast<LPCWSTR>(SND_ALIAS_SYSTEMDEFAULT), nullptr, SND_ALIAS_ID | SND_ASYNC);
    }
#else
    Q_UNUSED(sound)
#endif
}

void os::setForegroundWindow(QWidget *window)
{
#ifdef Q_OS_WIN
    ShowWindow((HWND)window->winId(), SW_RESTORE);
    SetForegroundWindow((HWND)window->winId());
#else
    Q_UNUSED(window)
#endif
}

void os::setStartup(bool startup, bool hide)
{
    QString lightscreen = QDir::toNativeSeparators(qApp->applicationFilePath());

    if (hide) {
        lightscreen.append(" -h");
    }

#ifdef Q_OS_WIN
    // Windows startup settings
    QSettings init("Microsoft", "Windows");
    init.beginGroup("CurrentVersion");
    init.beginGroup("Run");

    if (startup) {
        init.setValue("Lightscreen", lightscreen);
    } else {
        init.remove("Lightscreen");
    }

    init.endGroup();
    init.endGroup();
#endif

#if defined(Q_OS_LINUX)
    const QString autostartPath = QDir::homePath() + "/.config/autostart";
    QFile desktopFile(autostartPath + "/lightscreen.desktop");

    desktopFile.remove();

    if (startup) {
        const QString entry = "[Desktop Entry]\nType=Application\nName=Lightscreen\nExec="
                              + desktopExec(qApp->applicationFilePath(), hide) + "\n";

        if (!QDir().mkpath(autostartPath) || !desktopFile.open(QIODevice::WriteOnly)
            || desktopFile.write(entry.toUtf8()) < 0) {
            qWarning("Could not write %s", qPrintable(desktopFile.fileName()));
        }
    }
#endif
}

QString os::desktopExec(const QString &program, bool hide)
{
    // Inside double quotes, ", `, $ and \ take a backslash; the string-value escape then doubles every backslash.
    QString quoted = program;
    quoted.replace('\\', "\\\\");
    quoted.replace('"', "\\\"").replace('`', "\\`").replace('$', "\\$");
    quoted.replace('\\', "\\\\");
    quoted.replace('%', "%%");

    QString exec = '"' + quoted + '"';

    if (hide) {
        exec.append(" -h");
    }

    return exec;
}

QGraphicsEffect *os::shadow(const QColor &color, int blurRadius, int offset)
{
    QGraphicsDropShadowEffect *shadowEffect = new QGraphicsDropShadowEffect;
    shadowEffect->setBlurRadius(blurRadius);
    shadowEffect->setOffset(offset);
    shadowEffect->setColor(color);

    return shadowEffect;
}

QIcon os::icon(const QString &name, QColor backgroundColor)
{
    if (!backgroundColor.isValid()) {
        backgroundColor = qApp->palette().color(QPalette::Button);
    }

    if (backgroundColor.value() > 125) {
        return QIcon(":/icons/" + name);
    } else {
        return QIcon(":/icons/inv/" + name);
    }
}

#ifdef Q_OS_LINUX
Display *os::x11Display()
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->display() : nullptr;
}

Window os::x11RootWindow()
{
    return DefaultRootWindow(os::x11Display());
}

// Taken from KSnapshot. Oh KDE, what would I do without you :D
Window os::findRealWindow(Window w, int depth)
{
    if (depth > 5) {
        return None;
    }

    static Atom wm_state = XInternAtom(os::x11Display(), "WM_STATE", False);
    Atom type;
    int format;
    unsigned long nitems, after;
    unsigned char *prop;

    if (XGetWindowProperty(os::x11Display(), w, wm_state, 0, 0, False, AnyPropertyType,
                           &type, &format, &nitems, &after, &prop) == Success) {
        if (prop != NULL) {
            XFree(prop);
        }

        if (type != None) {
            return w;
        }
    }

    Window root, parent;
    Window *children;
    unsigned int nchildren;
    Window ret = None;

    if (XQueryTree(os::x11Display(), w, &root, &parent, &children, &nchildren) != 0) {
        for (unsigned int i = 0;
                i < nchildren && ret == None;
                ++i) {
            ret = os::findRealWindow(children[ i ], depth + 1);
        }

        if (children != NULL) {
            XFree(children);
        }
    }

    return ret;
}

Window os::windowUnderCursor(bool includeDecorations)
{
    Window root;
    Window child;
    uint mask;
    int rootX, rootY, winX, winY;

    XQueryPointer(os::x11Display(), os::x11RootWindow(), &root, &child,
                  &rootX, &rootY, &winX, &winY, &mask);

    if (child == None) {
        child = os::x11RootWindow();
    }

    if (!includeDecorations) {
        Window real_child = os::findRealWindow(child);

        if (real_child != None) {  // test just in case
            child = real_child;
        }
    }

    return child;
}
#endif
