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
#include <QActionGroup>
#include <QDate>
#include <QDesktopServices>
#include <QScreen>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QKeyEvent>

#ifdef Q_OS_WIN
    #include <windows.h>
#endif

//
//Lightscreen includes
//
#include <lightscreenwindow.h>
#include <dialogs/areadialog.h>
#include <dialogs/optionsdialog.h>
#include <dialogs/previewdialog.h>
#include <dialogs/historydialog.h>

#include <tools/hdrcapture.h>
#include <tools/optimizer.h>
#include <tools/os.h>
#include <tools/screenshot.h>
#include <tools/screenshotmanager.h>
#include <tools/settings.h>
#include <tools/UGlobalHotkey/uglobalhotkeys.h>
#include <tools/uploader/uploader.h>

#include <updater/updater.h>

LightscreenWindow::LightscreenWindow(QWidget *parent) :
    QMainWindow(parent),
    mDoCache(false),
    mHideTrigger(false),
    mReviveMain(false),
    mWasVisible(true),
    mLastMessage(0),
    mPendingScreenshots(0),
    mLastScreenshot()
{
    ui.setupUi(this);

    ui.screenPushButton->setIcon(os::icon("screen.big"));
    ui.areaPushButton->setIcon(os::icon("area.big"));
    ui.windowPushButton->setIcon(os::icon("pickWindow.big"));

    ui.optionsPushButton->setIcon(os::icon("configure"));
    ui.folderPushButton->setIcon(os::icon("folder"));
    ui.uploadPushButton->setIcon(os::icon("upload"));

    createUploadMenu();

    setMinimumSize(size());
    setWindowFlags(windowFlags() ^ Qt::WindowMaximizeButtonHint);

    // Actions
    connect(ui.screenPushButton, &QPushButton::clicked, this, &LightscreenWindow::screenHotkey);
    connect(ui.areaPushButton  , &QPushButton::clicked, this, &LightscreenWindow::areaHotkey);
    connect(ui.windowPushButton, &QPushButton::clicked, this, &LightscreenWindow::windowPickerHotkey);

    connect(ui.optionsPushButton, &QPushButton::clicked, this, &LightscreenWindow::showOptions);
    connect(ui.folderPushButton , &QPushButton::clicked, this, &LightscreenWindow::goToFolder);

    // Shortcuts
    mGlobalHotkeys = new UGlobalHotkeys(this);

    connect(mGlobalHotkeys, &UGlobalHotkeys::activated, [&](size_t id) {
        action(id);
    });

    // Uploader
    connect(ScreenshotManager::instance()->uploader(), &Uploader::progressChanged, this, &LightscreenWindow::uploadProgress);
    connect(ScreenshotManager::instance()->uploader(), &Uploader::done           , this, &LightscreenWindow::showUploaderMessage);
    connect(ScreenshotManager::instance()->uploader(), &Uploader::error          , this, &LightscreenWindow::showUploaderError);

    // Manager
    connect(ScreenshotManager::instance(), &ScreenshotManager::areaSelection,     this, &LightscreenWindow::selectArea);
    connect(ScreenshotManager::instance(), &ScreenshotManager::confirm,           this, &LightscreenWindow::preview);
    connect(ScreenshotManager::instance(), &ScreenshotManager::windowCleanup,     this, &LightscreenWindow::cleanup);
    connect(ScreenshotManager::instance(), &ScreenshotManager::activeCountChange, this, &LightscreenWindow::updateStatus);
    connect(ScreenshotManager::instance()->optimizer(), &Optimizer::pendingChanged, this, &LightscreenWindow::updateStatus);

    // Bug 9 crash tripwire: a marker left by the last run means it died inside the HDR capture path (a graphics driver
    // fault, which nothing in the app can catch), so HDR capture is turned off on this machine and the user is told
    // once. The options' "HDR capture" setting turns it back on.
    HdrCapture::setCrashMarkerPath(QDir(QDir::tempPath()).filePath(QStringLiteral("lightscreen-hdr-capture-running")));

    if (HdrCapture::takeCrashMarker()) {
        settings()->setValue("options/hdrCapture", 1);
        QTimer::singleShot(0, this, [this] {
            QMessageBox::warning(this, tr("Lightscreen Rd"),
                                 tr("Lightscreen Rd stopped during an HDR capture the last time it ran, so HDR capture "
                                    "is now off and screenshots are taken as in earlier versions. To try it again, "
                                    "set \"HDR capture\" to Automatic on the Options tab of the options."));
        });
    }

    // PNG optimizations left over when the app last stopped run again.
    QTimer::singleShot(0, ScreenshotManager::instance()->optimizer(), &Optimizer::resume);

    if (!settings()->contains("file/format")) {
        showOptions();  // There are no options (or the options config is invalid or incomplete)
    } else {
        QTimer::singleShot(0   , this, &LightscreenWindow::applySettings);
        QTimer::singleShot(5000, this, &LightscreenWindow::checkForUpdates);
    }
}

LightscreenWindow::~LightscreenWindow()
{
    settings()->setValue("lastScreenshot", mLastScreenshot);
    settings()->sync();
    mGlobalHotkeys->unregisterAllHotkeys();
}

void LightscreenWindow::action(int mode)
{
    if (mode <= Screenshot::SelectedWindow) {
        screenshotAction((Screenshot::Mode)mode);
    } else if (mode == ShowMainWindow) {
        show();
    } else if (mode == OpenScreenshotFolder) {
        goToFolder();
    } else {
        qWarning() << "Unknown hotkey ID: " << mode;
    }
}

void LightscreenWindow::areaHotkey()
{
    screenshotAction(Screenshot::SelectedArea);
}

void LightscreenWindow::checkForUpdates()
{
    if (Settings::value(settings(), "options/disableUpdater").toBool()) {
        return;
    }

    if (settings()->value("lastUpdateCheck").toInt() + 7
            > QDate::currentDate().dayOfYear()) {
        return;    // If 7 days have not passed since the last update check.
    }

    mUpdater = new Updater(ScreenshotManager::instance()->uploader()->network(), this);

    connect(mUpdater, &Updater::done, this, &LightscreenWindow::updaterDone);
    connect(mUpdater, &Updater::failed, this, &LightscreenWindow::updaterFailed);
    mUpdater->check();
}

void LightscreenWindow::cleanup(const Screenshot::Options &options)
{
    // Reversing settings, once no other request is waiting to be taken with the window hidden
    if (Settings::value(settings(), "options/hide").toBool() && mPendingScreenshots == 0) {
#ifndef Q_OS_LINUX // X is not quick enough and the notification ends up everywhere but in the icon
        if (Settings::value(settings(), "options/tray").toBool() && mTrayIcon) {
            mTrayIcon->show();
        }
#endif

        if (mPreviewDialog) {
            if (mPreviewDialog->count() <= 1 && mWasVisible) {
                show();
            }

            mPreviewDialog->show();
        } else if (mWasVisible) {
            show();
        }

        mHideTrigger = false;
    }

    if (Settings::value(settings(), "options/tray").toBool() && mTrayIcon) {
        notify(options.result);

        if (Settings::value(settings(), "options/message").toBool() && options.file && !options.upload) {
            // This message wll get shown only when messages are enabled and the file won't get another upload pop-up soon.
            showScreenshotMessage(options.result, options.fileName);
        }
    }

    if (Settings::value(settings(), "options/playSound").toBool()) {
        os::playSound(options.result == Screenshot::Success ? os::Sound::Screenshot : os::Sound::Error);
    }

    updateStatus();

    if (options.result != Screenshot::Success) {
        return;
    }

    mLastScreenshot = options.fileName;
}

void LightscreenWindow::closeToTrayWarning()
{
    if (!Settings::value(settings(), "options/closeToTrayWarning").toBool()) {
        return;
    }

    mLastMessage = 3;
    mTrayIcon->showMessage(tr("Closed to tray"), tr("Lightscreen will keep running, you can disable this in the options menu."));
    settings()->setValue("options/closeToTrayWarning", false);
}

bool LightscreenWindow::closingWithoutTray()
{
    if (Settings::value(settings(), "options/disableHideAlert").toBool()) {
        return false;
    }

    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("Lightscreen Rd"));
    msgBox.setText(tr("You have chosen to hide Lightscreen when there's no system tray icon, so you will not be able to access the program <b>unless you have selected a hotkey to do so</b>.<br>What do you want to do?"));
    msgBox.setIcon(QMessageBox::Warning);

    msgBox.setStyleSheet("QPushButton { padding: 4px 8px; }");

    auto enableButton      = msgBox.addButton(tr("Hide but enable tray"), QMessageBox::ActionRole);
    auto enableQuietButton = msgBox.addButton(tr("Hide and don't warn"), QMessageBox::ActionRole);
    auto hideButton        = msgBox.addButton(tr("Just hide"), QMessageBox::ActionRole);
    auto abortButton       = msgBox.addButton(QMessageBox::Cancel);

    Q_UNUSED(abortButton);

    msgBox.exec();

    if (msgBox.clickedButton() == hideButton) {
        return true;
    } else if (msgBox.clickedButton() == enableQuietButton) {
        settings()->setValue("options/disableHideAlert", true);
        applySettings();
        return true;
    } else if (msgBox.clickedButton() == enableButton) {
        settings()->setValue("options/tray", true);
        applySettings();
        return true;
    }

    return false; // Cancel.
}

void LightscreenWindow::createUploadMenu()
{
    auto uploadMenu = new QMenu(tr("Upload"));

    auto uploadAction = new QAction(os::icon("upload"), tr("&Upload last"), uploadMenu);
    uploadAction->setToolTip(tr("Upload the last screenshot you took"));
    connect(uploadAction, &QAction::triggered, this, &LightscreenWindow::uploadLast);

    auto cancelAction = new QAction(os::icon("no"), tr("&Cancel upload"), uploadMenu);
    cancelAction->setToolTip(tr("Cancel the currently uploading screenshots"));
    cancelAction->setEnabled(false);

    connect(this, &LightscreenWindow::uploading, cancelAction, &QAction::setEnabled);
    connect(cancelAction, &QAction::triggered, this, &LightscreenWindow::uploadCancel);

    auto historyAction = new QAction(os::icon("view-history"), tr("View &History"), uploadMenu);
    connect(historyAction, &QAction::triggered, this, &LightscreenWindow::showHistoryDialog);

    uploadMenu->addAction(uploadAction);
    uploadMenu->addAction(cancelAction);
    uploadMenu->addAction(historyAction);
    uploadMenu->addSeparator();

    connect(uploadMenu, &QMenu::aboutToShow, this, [&, uploadMenu] {
        uploadMenu->actions().at(0)->setEnabled(!mLastScreenshot.isEmpty());
    });

    ui.uploadPushButton->setMenu(uploadMenu);
}

void LightscreenWindow::goToFolder()
{
#ifdef Q_OS_WIN
    if (!mLastScreenshot.isEmpty() && QFile::exists(mLastScreenshot)) {
        QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(mLastScreenshot)});
    } else {
#endif
        QDir path(Settings::value(settings(), "file/target", ScreenshotManager::instance()->portableMode()).toString());

        // We might want to go to the folder without it having been created by taking a screenshot yet.
        if (!path.exists()) {
            path.mkpath(path.absolutePath());
        }

        QDesktopServices::openUrl(QUrl::fromLocalFile(path.absolutePath() + QDir::separator()));
#ifdef Q_OS_WIN
    }
#endif
}

void LightscreenWindow::messageClicked()
{
    if (mLastMessage == 1) {
        goToFolder();
    } else if (mLastMessage == 3) {
        QTimer::singleShot(0, this, &LightscreenWindow::showOptions);
    } else {
        QDesktopServices::openUrl(QUrl(ScreenshotManager::instance()->uploader()->lastUrl()));
    }
}

void LightscreenWindow::executeArgument(const QString &message)
{
    if (message == "--wake") {
        show();
        os::setForegroundWindow(this);
        qApp->alert(this, 2000);
    } else if (message == "--screen") {
        screenshotAction(Screenshot::WholeScreen);
    } else if (message == "--area") {
        screenshotAction(Screenshot::SelectedArea);
    } else if (message == "--activewindow") {
        screenshotAction(Screenshot::ActiveWindow);
    } else if (message == "--pickwindow") {
        screenshotAction(Screenshot::SelectedWindow);
    } else if (message == "--folder") {
        action(OpenScreenshotFolder);
    } else if (message == "--uploadlast") {
        uploadLast();
    } else if (message == "--viewhistory") {
        showHistoryDialog();
    } else if (message == "--options") {
        showOptions();
    } else if (message == "--quit") {
        // Queued, so that it also works before the event loop starts (main runs our own arguments before exec,
        // which would reset an immediate quit; bug 29). exit() rather than quit(): Qt 6's quit() first sends the
        // windows a close event, and with "close hides the window" on and no tray icon that close asks whether to
        // hide instead, so --quit waited on a dialog (found in S06, where closeHide became on by default).
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(0); });
    }
}

void LightscreenWindow::executeArguments(const QStringList &arguments)
{
    // If we just have the default argument, call "--wake"
    if (arguments.count() == 1 && (arguments.at(0) == qApp->arguments().at(0) || arguments.at(0).contains(QFileInfo(qApp->applicationFilePath()).fileName()))) {
        executeArgument("--wake");
        return;
    }

    for (auto argument : arguments) {
        executeArgument(argument);
    }
}

void LightscreenWindow::notify(const Screenshot::Result &result)
{
    switch (result) {
    case Screenshot::Success:
        if (mTrayIcon) {
            mTrayIcon->setIcon(QIcon(":/icons/lightscreen.yes"));
        }

        mTaskbar.setOverlayIcon(os::icon("yes"));

        setWindowTitle(tr("Success!"));
        break;
    case Screenshot::Failure:
        if (mTrayIcon) {
            mTrayIcon->setIcon(QIcon(":/icons/lightscreen.no"));
        }

        setWindowTitle(tr("Failed!"));

        mTaskbar.setOverlayIcon(os::icon("no"));

        break;
    case Screenshot::Cancel:
        setWindowTitle(tr("Cancelled!"));
        break;
    }

    QTimer::singleShot(2000, this, &LightscreenWindow::restoreNotification);
}

void LightscreenWindow::preview(Screenshot *screenshot)
{
    if (screenshot->options().preview) {
        if (!mPreviewDialog) {
            mPreviewDialog = new PreviewDialog(this);
        }

        mPreviewDialog->add(screenshot);
    } else {
        screenshot->confirm(true);
    }
}

void LightscreenWindow::selectArea(Screenshot *screenshot)
{
    AreaDialog selector(screenshot->pixmap(), screenshot->options(), screenshot->deviceGeometry());

    // F5 grabs the desktop again, behind the dialog.
    connect(&selector, &AreaDialog::refreshRequested, screenshot, [&selector, screenshot] {
        screenshot->refresh();
        selector.setPixmap(screenshot->pixmap());
    });

    if (selector.exec() == QDialog::Accepted) {
        screenshot->setArea(selector.resultRect());
    } else {
        screenshot->discard();
    }
}

void LightscreenWindow::quit()
{
    settings()->setValue("position", pos());

    int answer = 0;
    QString doing;

    if (ScreenshotManager::instance()->activeCount() > 0) {
        doing = tr("processing");
    }

    if (ScreenshotManager::instance()->uploader()->uploading() > 0) {
        if (doing.isEmpty()) {
            doing = tr("uploading");
        } else {
            doing = tr("processing and uploading");
        }
    }

    if (!doing.isEmpty()) {
        answer = QMessageBox::question(this,
                                       tr("Are you sure you want to quit?"),
                                       tr("Lightscreen is currently %1 screenshots. Are you sure you want to quit?").arg(doing),
                                       tr("Quit"),
                                       tr("Don't Quit"));
    }

    if (answer == 0) {
        emit finished();
    }
}

void LightscreenWindow::restoreNotification()
{
    if (mTrayIcon) {
        mTrayIcon->setIcon(QIcon(":/icons/lightscreen.small"));
    }

    mTaskbar.clearOverlayIcon();
    mTaskbar.setProgressState(Taskbar::Progress::None);

    updateStatus();
}

void LightscreenWindow::screenshotAction(Screenshot::Mode mode)
{
    if (mode == Screenshot::None) {
        return;
    }

    int delayms = -1;

    bool optionsHide = Settings::value(settings(), "options/hide").toBool(); // Option cache, used a couple of times.

    if (!mHideTrigger) {
        mWasVisible = isVisible();
        mHideTrigger = true;
    }

    // Applying pre-screenshot settings
    if (optionsHide) {
        hide();

#ifndef Q_OS_LINUX // X is not quick enough and the notification ends up everywhere but in the icon
        if (mTrayIcon) {
            mTrayIcon->hide();
        }
#endif
    }

    // Screenshot delay
    delayms = Settings::value(settings(), "options/delay").toInt();
    delayms = delayms * 1000; // Converting the delay to milliseconds.

    delayms += 400;

    if (optionsHide && mPreviewDialog) {
        if (mPreviewDialog->count() >= 1) {
            mPreviewDialog->hide();
        }
    }

    // Each request waits out its own delay and keeps its own mode.
    ++mPendingScreenshots;

    QTimer::singleShot(delayms, this, [this, mode] {
        --mPendingScreenshots;
        takeScreenshot(mode);
    });
}

void LightscreenWindow::takeScreenshot(Screenshot::Mode mode)
{
    static Screenshot::Options options;

    if (!mDoCache) {
        // Populating the option object that will then be passed to the screenshot engine (sounds fancy huh?)
        options.file           = Settings::value(settings(), "file/enabled").toBool();
        options.format         = Screenshot::formatFromSetting(Settings::value(settings(), "file/format"));
        options.prefix         = Settings::value(settings(), "file/prefix").toString();

        QDir dir(Settings::value(settings(), "file/target", ScreenshotManager::instance()->portableMode()).toString());
        dir.makeAbsolute();

        options.directory      = dir;

        options.quality        = Settings::value(settings(), "options/quality").toInt();
        options.currentMonitor = Settings::value(settings(), "options/currentMonitor").toBool();
        options.clipboard      = Settings::value(settings(), "options/clipboard").toBool();
        options.urlClipboard   = Settings::value(settings(), "options/urlClipboard").toBool();
        options.preview        = Settings::value(settings(), "options/preview").toBool();
        options.magnify        = Settings::value(settings(), "options/magnify").toBool();
        options.cursor         = Settings::value(settings(), "options/cursor").toBool();
        options.saveAs         = Settings::value(settings(), "options/saveAs").toBool();
        options.animations     = Settings::value(settings(), "options/animations")  .toBool();
        options.replace        = Settings::value(settings(), "options/replace").toBool();
        options.upload         = Settings::value(settings(), "options/uploadAuto").toBool();
        options.optimize       = Settings::value(settings(), "options/optimize").toBool();

        options.uploadService  = Uploader::serviceName(Settings::value(settings(), "upload/service").toInt());

        Screenshot::NamingOptions namingOptions;
        namingOptions.naming       = (Screenshot::Naming) Settings::value(settings(), "file/naming").toInt();
        namingOptions.leadingZeros = Settings::value(settings(), "options/naming/leadingZeros").toInt();
        namingOptions.flip         = Settings::value(settings(), "options/flip").toBool();
        namingOptions.dateFormat   = Settings::value(settings(), "options/naming/dateFormat").toString();

        options.namingOptions = namingOptions;

        mDoCache = true;
    }

    options.mode = mode;

    ScreenshotManager::instance()->take(options);
}

void LightscreenWindow::screenshotActionTriggered(QAction *action)
{
    screenshotAction(action->data().value<Screenshot::Mode>());
}

void LightscreenWindow::screenHotkey()
{
    screenshotAction(Screenshot::WholeScreen);
}

void LightscreenWindow::showHotkeyError(const QStringList &hotkeys)
{
    static bool dontShow = false;

    if (dontShow) {
        return;
    }

    QString messageText;

    messageText = tr("Some hotkeys could not be registered, they might already be in use");

    if (hotkeys.count() > 1) {
        messageText += tr("<br>The failed hotkeys are the following:") + "<ul>";

        for (auto hotkey : hotkeys) {
            messageText += QString("%1%2%3").arg("<li><b>").arg(hotkey).arg("</b></li>");
        }

        messageText += "</ul>";
    } else {
        messageText += tr("<br>The failed hotkey is <b>%1</b>").arg(hotkeys[0]);
    }

    messageText += tr("<br><i>What do you want to do?</i>");

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Lightscreen Rd"));
    msgBox.setText(messageText);

    QPushButton *changeButton  = msgBox.addButton(tr("Change") , QMessageBox::ActionRole);
    QPushButton *disableButton = msgBox.addButton(tr("Disable"), QMessageBox::ActionRole);
    QPushButton *exitButton    = msgBox.addButton(tr("Quit")   , QMessageBox::ActionRole);

    msgBox.exec();

    if (msgBox.clickedButton() == exitButton) {
        dontShow = true;
        QTimer::singleShot(10, this, &LightscreenWindow::quit);
    } else if (msgBox.clickedButton() == changeButton) {
        showOptions();
    } else if (msgBox.clickedButton() == disableButton) {
        for (auto hotkey : hotkeys) {
            settings()->setValue(QString("actions/%1/enabled").arg(hotkey), false);
        }
    }
}

void LightscreenWindow::showHistoryDialog()
{
    HistoryDialog historyDialog(this);
    historyDialog.exec();

    updateStatus();
}

void LightscreenWindow::showOptions()
{
    mGlobalHotkeys->unregisterAllHotkeys();
    QPointer<OptionsDialog> optionsDialog = new OptionsDialog(this);

    optionsDialog->exec();
    optionsDialog->deleteLater();

    applySettings();
}

void LightscreenWindow::showScreenshotMessage(const Screenshot::Result &result, const QString &fileName)
{
    if (result == Screenshot::Cancel) {
        return;
    }

    // Showing message.
    QString title;
    QString message;

    if (result == Screenshot::Success) {
        title = QFileInfo(fileName).fileName();

        if (Settings::value(settings(), "file/target", ScreenshotManager::instance()->portableMode()).toString().isEmpty()) {
            message = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
        } else {
            message = tr("Saved to \"%1\"").arg(Settings::value(settings(), "file/target", ScreenshotManager::instance()->portableMode()).toString());
        }
    } else {
        title   = tr("The screenshot was not taken");
        message = tr("An error occurred.");
    }

    mLastMessage = 1;
    mTrayIcon->showMessage(title, message);
}

void LightscreenWindow::showUploaderError(const QString &error)
{
    mLastMessage = -1;
    updateStatus();

    if (mTrayIcon && !error.isEmpty() && Settings::value(settings(), "options/message").toBool()) {
        mTrayIcon->showMessage(tr("Upload error"), error);
    }

    notify(Screenshot::Failure);
}

void LightscreenWindow::showUploaderMessage(const QString &fileName, const QString &url)
{
    if (mTrayIcon && Settings::value(settings(), "options/message").toBool() && !url.isEmpty()) {
        QString screenshot = QFileInfo(fileName).fileName();

        if (screenshot.startsWith(".lstemp.")) {
            screenshot = tr("Screenshot");
        }

        mLastMessage = 2;
        mTrayIcon->showMessage(tr("%1 uploaded").arg(screenshot), tr("Click here to go to %1").arg(url));
    }

    updateStatus();
}

void LightscreenWindow::toggleVisibility()
{
    if (isVisible()) {
        hide();
    } else {
        show();
        os::setForegroundWindow(this);
    }
}

void LightscreenWindow::updateStatus()
{
    int uploadCount = ScreenshotManager::instance()->uploader()->uploading();
    int activeCount = ScreenshotManager::instance()->activeCount();
    int optimizeCount = ScreenshotManager::instance()->optimizer()->pending();

    mTaskbar.setProgressState(Taskbar::Progress::Paused);

    if (uploadCount > 0) {
        setStatus(tr("%1 uploading").arg(uploadCount));

        mTaskbar.setProgressState(Taskbar::Progress::Normal);

        emit uploading(true);
    } else if (optimizeCount > 0) {
        setStatus(tr("%1 optimizing").arg(optimizeCount));

        emit uploading(false);
    } else {
        if (activeCount > 1) {
            setStatus(tr("%1 processing").arg(activeCount));
        } else if (activeCount == 1) {
            setStatus(tr("processing"));
        } else {
            setStatus();

            mTaskbar.setProgressState(Taskbar::Progress::None);
        }

        emit uploading(false);
    }
}

void LightscreenWindow::updaterDone(bool newer, const QString &htmlUrl)
{
    mUpdater->deleteLater();

    settings()->setValue("lastUpdateCheck", QDate::currentDate().dayOfYear());

    if (!newer) {
        return;
    }

    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("Lightscreen Rd"));
    msgBox.setText(tr("There's a new version of Lightscreen available.<br>Would you like to see more information?<br>(<em>You can turn this notification off</em>)"));
    msgBox.setIcon(QMessageBox::Information);

    QPushButton *yesButton     = msgBox.addButton(QMessageBox::Yes);
    QPushButton *turnOffButton = msgBox.addButton(tr("Turn Off"), QMessageBox::ActionRole);
    QPushButton *remindButton  = msgBox.addButton(tr("Remind Me Later"), QMessageBox::RejectRole);

    Q_UNUSED(remindButton);

    msgBox.exec();

    if (msgBox.clickedButton() == yesButton) {
        QDesktopServices::openUrl(QUrl(htmlUrl));
    } else if (msgBox.clickedButton() == turnOffButton) {
        settings()->setValue("options/disableUpdater", true);
    }
}

// The silent check says nothing on failure; it still counts as the week's check so a dead network does not retry on every start.
void LightscreenWindow::updaterFailed()
{
    mUpdater->deleteLater();

    settings()->setValue("lastUpdateCheck", QDate::currentDate().dayOfYear());
}

void LightscreenWindow::upload(const QString &fileName)
{
    ScreenshotManager::instance()->uploader()->upload(fileName, Uploader::serviceName(Settings::value(settings(), "upload/service").toInt()));
}

void LightscreenWindow::uploadCancel()
{
    if (ScreenshotManager::instance()->uploader()->uploading() <= 0) {
        return;
    }

    int confirm = QMessageBox::question(this, tr("Upload cancel"), tr("Do you want to cancel all screenshot uploads?"), tr("Cancel"), tr("Don't Cancel"));

    if (confirm == 0) {
        ScreenshotManager::instance()->uploader()->cancel();
        updateStatus();
    }
}

void LightscreenWindow::uploadLast()
{
    upload(mLastScreenshot);
    updateStatus();
}

void LightscreenWindow::uploadProgress(int progress)
{
    mTaskbar.setProgressValue(progress);

    if (isVisible() && progress > 0) {
        int uploadCount = ScreenshotManager::instance()->uploader()->uploading();

        if (uploadCount > 1) {
            setWindowTitle(tr("%1% of %2 uploads - Lightscreen Rd").arg(progress).arg(uploadCount));
        } else {
            setWindowTitle(tr("%1% - Lightscreen Rd").arg(progress));
        }
    }
}

void LightscreenWindow::windowHotkey()
{
    screenshotAction(Screenshot::ActiveWindow);
}

void LightscreenWindow::windowPickerHotkey()
{
    screenshotAction(Screenshot::SelectedWindow);
}

void LightscreenWindow::applySettings()
{
    bool tray = Settings::value(settings(), "options/tray").toBool();

    if (tray && !mTrayIcon) {
        createTrayIcon();
        mTrayIcon->setVisible(true);
    } else if (!tray && mTrayIcon) {
        mTrayIcon->setVisible(false);
    }

    connectHotkeys();

    mDoCache = false;

    if (settings()->value("lastScreenshot").isValid() && mLastScreenshot.isEmpty()) {
        mLastScreenshot = settings()->value("lastScreenshot").toString();
    }

    os::setStartup(Settings::value(settings(), "options/startup").toBool(), Settings::value(settings(), "options/startupHide").toBool());

    HdrCapture::setEnabled(Settings::value(settings(), "options/hdrCapture").toInt() == 0);
    HdrCapture::setCheckAgainstGdi(Settings::value(settings(), "options/hdrCheckAgainstGdi").toBool());
}

void LightscreenWindow::connectHotkeys()
{
    const QStringList actions = {"screen", "window", "area", "windowPicker", "open", "directory"};
    QStringList failed;
    size_t id = Screenshot::WholeScreen;

    for (auto action : actions) {
        if (Settings::value(settings(), "actions/" + action + "/enabled").toBool()) {
            if (!mGlobalHotkeys->registerHotkey(Settings::value(settings(), "actions/" + action + "/hotkey").toString(), id)) {
                failed << action;
            }
        }

        id++;
    }

    if (!failed.isEmpty()) {
        showHotkeyError(failed);
    }
}

void LightscreenWindow::createTrayIcon()
{
    mTrayIcon = new QSystemTrayIcon(QIcon(":/icons/lightscreen.small"), this);
    updateStatus();

    connect(mTrayIcon, &QSystemTrayIcon::messageClicked, this, &LightscreenWindow::messageClicked);
    connect(mTrayIcon, &QSystemTrayIcon::activated     , this, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::DoubleClick) return;
        toggleVisibility();
    });

    auto hideAction = new QAction(QIcon(":/icons/lightscreen.small"), tr("Show&/Hide"), mTrayIcon);
    connect(hideAction, &QAction::triggered, this, &LightscreenWindow::toggleVisibility);

    auto screenAction = new QAction(os::icon("screen"), tr("&Screen"), mTrayIcon);
    screenAction->setData(QVariant::fromValue<Screenshot::Mode>(Screenshot::WholeScreen));

    auto windowAction = new QAction(os::icon("window"), tr("Active &Window"), this);
    windowAction->setData(QVariant::fromValue<Screenshot::Mode>(Screenshot::ActiveWindow));

    auto windowPickerAction = new QAction(os::icon("pickWindow"), tr("&Pick Window"), this);
    windowPickerAction->setData(QVariant::fromValue<Screenshot::Mode>(Screenshot::SelectedWindow));

    auto areaAction = new QAction(os::icon("area"), tr("&Area"), mTrayIcon);
    areaAction->setData(QVariant::fromValue<Screenshot::Mode>(Screenshot::SelectedArea));

    auto screenshotGroup = new QActionGroup(mTrayIcon);
    screenshotGroup->addAction(screenAction);
    screenshotGroup->addAction(areaAction);
    screenshotGroup->addAction(windowAction);
    screenshotGroup->addAction(windowPickerAction);

    connect(screenshotGroup, &QActionGroup::triggered, this, &LightscreenWindow::screenshotActionTriggered);

    // Duplicated for the screenshot button :(
    auto uploadAction = new QAction(os::icon("upload"), tr("&Upload last"), mTrayIcon);
    uploadAction->setToolTip(tr("Upload the last screenshot you took"));
    connect(uploadAction, &QAction::triggered, this, &LightscreenWindow::uploadLast);

    auto cancelAction = new QAction(os::icon("no"), tr("&Cancel upload"), mTrayIcon);
    cancelAction->setToolTip(tr("Cancel the currently uploading screenshots"));
    cancelAction->setEnabled(false);
    connect(this, &LightscreenWindow::uploading, cancelAction, &QAction::setEnabled);
    connect(cancelAction, &QAction::triggered, this, &LightscreenWindow::uploadCancel);

    auto historyAction = new QAction(os::icon("view-history"), tr("View History"), mTrayIcon);
    connect(historyAction, &QAction::triggered, this, &LightscreenWindow::showHistoryDialog);
    //

    auto optionsAction = new QAction(os::icon("configure"), tr("View &Options"), mTrayIcon);
    connect(optionsAction, &QAction::triggered, this, &LightscreenWindow::showOptions);

    auto goAction = new QAction(os::icon("folder"), tr("&Go to Folder"), mTrayIcon);
    connect(goAction, &QAction::triggered, this, &LightscreenWindow::goToFolder);

    auto quitAction = new QAction(tr("&Quit"), mTrayIcon);
    connect(quitAction, &QAction::triggered, this, &LightscreenWindow::quit);

    auto screenshotMenu = new QMenu(tr("Screenshot"));
    screenshotMenu->addAction(screenAction);
    screenshotMenu->addAction(areaAction);
    screenshotMenu->addAction(windowAction);
    screenshotMenu->addAction(windowPickerAction);

    // Duplicated for the screenshot button :(
    auto uploadMenu = new QMenu(tr("Upload"));
    uploadMenu->addAction(uploadAction);
    uploadMenu->addAction(cancelAction);
    uploadMenu->addAction(historyAction);
    uploadMenu->addSeparator();

    auto trayIconMenu = new QMenu;
    trayIconMenu->addAction(hideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addMenu(uploadMenu);
    trayIconMenu->addSeparator();
    trayIconMenu->addMenu(screenshotMenu);
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(goAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    mTrayIcon->setContextMenu(trayIconMenu);
}

void LightscreenWindow::setStatus(QString status)
{
    if (status.isEmpty()) {
        status = tr("Lightscreen Rd");
    } else {
        status += tr(" - Lightscreen Rd");
    }

    if (mTrayIcon) {
        mTrayIcon->setToolTip(status);
    }

    setWindowTitle(status);
}

QSettings *LightscreenWindow::settings() const
{
    return ScreenshotManager::instance()->settings();
}

// Event handling
bool LightscreenWindow::event(QEvent *event)
{
    if (event->type() == QEvent::Show) {
        QPoint savedPosition = settings()->value("position").toPoint();

        if (!savedPosition.isNull() && QGuiApplication::primaryScreen()->availableGeometry().contains(QRect(savedPosition, size()))) {
            move(savedPosition);
        }

        mTaskbar.setWindow(windowHandle());
    } else if (event->type() == QEvent::Hide) {
        settings()->setValue("position", pos());
    } else if (event->type() == QEvent::Close) {
        if (Settings::value(settings(), "options/tray").toBool() && Settings::value(settings(), "options/closeHide").toBool()) {
            closeToTrayWarning();
            hide();
        } else if (Settings::value(settings(), "options/closeHide").toBool()) {
            if (closingWithoutTray()) {
                hide();
            }
        } else {
            quit();
        }
    } else if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (!keyEvent->modifiers() && keyEvent->key() == Qt::Key_Escape) {
            keyEvent->ignore();

            if (isVisible()) {
                toggleVisibility();
            }

            return false;
        }
    } else if (event->type() == QEvent::LanguageChange) {
        ui.retranslateUi(this);
        resize(minimumSizeHint());
    }


    return QMainWindow::event(event);
}
