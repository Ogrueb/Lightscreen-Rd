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
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QRubberBand>
#include <QRubberBand>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

#include <tools/windowpicker.h>
#include <tools/os.h>

#if defined(Q_OS_WIN)
    #include <windows.h>

    #ifdef _WIN64
        #define GCL_HICON GCLP_HICON
        #define GCL_HICONSM GCLP_HICONSM
    #endif

#elif defined(Q_OS_LINUX)
    #include <X11/X.h>
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xatom.h>
#endif

WindowPicker::WindowPicker() : QWidget(0), mCrosshair(":/icons/picker"), mWindowLabel(nullptr), mTaken(false), mCurrentWindow(0)
{
#if defined(Q_OS_WIN)
    setWindowFlags(Qt::SplashScreen | Qt::WindowStaysOnTopHint);
#elif defined(Q_OS_LINUX)
    setWindowFlags(Qt::WindowStaysOnTopHint);
#endif

    setWindowTitle(tr("Lightscreen Rd Window Picker"));
    setStyleSheet("QWidget { color: #000; } #frame { padding: 7px 10px; border: 4px solid #232323; background-color: rgba(250, 250, 250, 255); }");

    QLabel *helpLabel = new QLabel(tr("Grab the window picker by clicking and holding down the mouse button, then drag it to the window of your choice and release it to capture."), this);
    helpLabel->setMinimumWidth(400);
    helpLabel->setMaximumWidth(400);
    helpLabel->setWordWrap(true);

    mWindowIcon = new QLabel(this);
    mWindowIcon->setMinimumSize(22, 22);
    mWindowIcon->setMaximumSize(22, 22);
    mWindowIcon->setScaledContents(true);

    mWindowLabel = new QLabel(tr(" - Start dragging to select windows"), this);
    mWindowLabel->setStyleSheet("font-weight: bold");

    mCrosshairLabel = new QLabel(this);
    mCrosshairLabel->setAlignment(Qt::AlignHCenter);
    mCrosshairLabel->setPixmap(mCrosshair);

    QPushButton *closeButton = new QPushButton(tr("Close"));
    connect(closeButton, &QPushButton::clicked, this, &WindowPicker::close);

    QHBoxLayout *windowLayout = new QHBoxLayout;
    windowLayout->addWidget(mWindowIcon);
    windowLayout->addWidget(mWindowLabel);
    windowLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch(0);
    buttonLayout->addWidget(closeButton);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *crosshairLayout = new QHBoxLayout;
    crosshairLayout->addStretch(0);
    crosshairLayout->addWidget(mCrosshairLabel);
    crosshairLayout->addStretch(0);
    crosshairLayout->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout *fl = new QVBoxLayout;
    fl->addWidget(helpLabel);
    fl->addLayout(windowLayout);
    fl->addLayout(crosshairLayout);
    fl->addLayout(buttonLayout);
    fl->setContentsMargins(0, 0, 0, 0);

    QFrame *frame = new QFrame(this);
    frame->setObjectName("frame");
    frame->setLayout(fl);

    QVBoxLayout *l = new QVBoxLayout;
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(frame);

    setLayout(l);

    resize(sizeHint());
    QScreen *cursorScreen = QGuiApplication::screenAt(QCursor::pos());

    if (!cursorScreen) {
        cursorScreen = QGuiApplication::primaryScreen();
    }

    move(cursorScreen->geometry().center() - QPoint(width() / 2, height() / 2));
    show();
}

WindowPicker::~WindowPicker()
{
    qApp->restoreOverrideCursor();
}

void WindowPicker::cancel()
{
    mWindowIcon->setPixmap(QPixmap());
    mCrosshairLabel->setPixmap(mCrosshair);
    qApp->restoreOverrideCursor();
}

void WindowPicker::closeEvent(QCloseEvent *)
{
    if (!mTaken) {
        emit pixmap(QPixmap());
    }

    qApp->restoreOverrideCursor();
    deleteLater();
}

#if defined(Q_OS_WIN)
// The cursor in physical screen pixels, as WindowFromPoint expects. The event's globalPosition() is in Qt's
// device-independent pixels, which differ from physical pixels at 125% or 150% scaling, so the picker used to pick the
// window at a point up and to the left of the cursor (found in the 2026-10-29 Windows pass, bug 42).
static POINT cursorPosition()
{
    POINT position = { 0, 0 };
    ::GetCursorPos(&position);
    return position;
}
#endif

void WindowPicker::mouseMoveEvent(QMouseEvent *event)
{
    QString windowName;

#if defined(Q_OS_WIN)
    Q_UNUSED(event);
    HWND cWindow = GetAncestor(WindowFromPoint(cursorPosition()), GA_ROOT);

    mCurrentWindow = (WId) cWindow;

    if (mCurrentWindow == winId()) {
        mWindowIcon->setPixmap(QPixmap());
        mWindowLabel->setText("");
        return;
    }

    // Text
    WCHAR str[60];
    HICON icon;

    ::GetWindowText((HWND)mCurrentWindow, str, 60);
    windowName = QString::fromWCharArray(str);
    ///

    // Retrieving the application icon
    icon = (HICON)::GetClassLongPtr((HWND)mCurrentWindow, GCLP_HICON);

    if (icon == NULL) {
        icon = (HICON)::GetClassLongPtr((HWND)mCurrentWindow, GCLP_HICONSM);
    }

    if (icon == NULL) {
        DWORD_PTR result = 0;

        if (::SendMessageTimeout((HWND)mCurrentWindow, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &result)) {
            icon = (HICON)result;
        }
    }

    if (icon != NULL) {
        mWindowIcon->setPixmap(os::pixmapFromHICON(icon));
    } else {
        mWindowIcon->setPixmap(QPixmap());
    }
#elif defined(Q_OS_LINUX)
    Window cWindow = os::windowUnderCursor(false);

    if (cWindow == mCurrentWindow) {
        return;
    }

    mCurrentWindow = cWindow;

    if (mCurrentWindow == winId()) {
        mWindowIcon->setPixmap(QPixmap());
        mWindowLabel->setText("");
        return;
    }

    // Getting the window name property.
    XTextProperty tp;
    char **text;
    int count;

    if (XGetTextProperty(os::x11Display(), cWindow, &tp, XA_WM_NAME) != 0 && tp.value != NULL) {
        if (tp.encoding == XA_STRING) {
            windowName = QString::fromLocal8Bit((const char *) tp.value);
        } else if (XmbTextPropertyToTextList(os::x11Display(), &tp, &text, &count) == Success &&
                   text != NULL && count > 0) {
            windowName = QString::fromLocal8Bit(text[0]);
            XFreeStringList(text);
        }

        XFree(tp.value);
    }

    // Retrieving the _NET_WM_ICON property.
    Atom type_ret = None;
    unsigned char *data = 0;
    int format = 0;
    unsigned long n = 0;
    unsigned long extra = 0;
    int width = 0;
    int height = 0;

    Atom _net_wm_icon = XInternAtom(os::x11Display(), "_NET_WM_ICON", False);

    if (XGetWindowProperty(os::x11Display(), cWindow, _net_wm_icon, 0, 1, False,
                           XA_CARDINAL, &type_ret, &format, &n, &extra, (unsigned char **)&data) == Success && data) {
        width = data[0];
        XFree(data);
    }

    if (XGetWindowProperty(os::x11Display(), cWindow, _net_wm_icon, 1, 1, False,
                           XA_CARDINAL, &type_ret, &format, &n, &extra, (unsigned char **)&data) == Success && data) {
        height = data[0];
        XFree(data);
    }

    if (XGetWindowProperty(os::x11Display(), cWindow, _net_wm_icon, 2, width * height, False,
                           XA_CARDINAL, &type_ret, &format, &n, &extra, (unsigned char **)&data) == Success && data) {
        QImage img(data, width, height, QImage::Format_ARGB32);
        mWindowIcon->setPixmap(QPixmap::fromImage(img));
        XFree(data);
    } else {
        mWindowIcon->setPixmap(QPixmap());
    }

#endif

    QString windowText;

    if (mWindowIcon->pixmap(Qt::ReturnByValue).isNull()) {
        windowText = QString(" - %1").arg(windowName);
    } else {
        windowText = windowName;
    }

    if (windowText == " - ") {
        mWindowLabel->setText("");
        return;
    }

    if (windowText.length() == 62) {
        mWindowLabel->setText(windowText + "...");
    } else {
        mWindowLabel->setText(windowText);
    }
}

void WindowPicker::mousePressEvent(QMouseEvent *event)
{
    qApp->setOverrideCursor(QCursor(mCrosshair));
    mCrosshairLabel->setMinimumWidth(mCrosshairLabel->width());
    mCrosshairLabel->setMinimumHeight(mCrosshairLabel->height());
    mCrosshairLabel->setPixmap(QPixmap());
    QWidget::mousePressEvent(event);
}

void WindowPicker::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
#if defined(Q_OS_WIN)
        HWND nativeWindow = GetAncestor(WindowFromPoint(cursorPosition()), GA_ROOT);
#elif defined(Q_OS_LINUX)
        Window nativeWindow = os::windowUnderCursor(false);
#endif

        if ((WId)nativeWindow == winId()) {
            cancel();
            return;
        }

        mTaken = true;

        setWindowFlags(windowFlags() ^ Qt::WindowStaysOnTopHint);
        close();

#ifdef Q_OS_LINUX
        emit pixmap(QGuiApplication::primaryScreen()->grabWindow(mCurrentWindow));
#else
        emit pixmap(os::grabWindow((WId)nativeWindow));
#endif

        return;
    }

    close();
}

