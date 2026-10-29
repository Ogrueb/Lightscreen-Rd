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
#include <QToolTip>
#include <QCompleter>
#include <QDate>
#include <QDesktopServices>
#include <QScreen>
#include <QFileDialog>
#include <QStringListModel>
#include <QKeyEvent>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QInputDialog>
#include <QMenu>
#include <QAction>

#include <QFutureWatcher>
#include <QtConcurrent>

#ifdef Q_OS_WIN
    #include <windows.h>
#endif

#include <dialogs/optionsdialog.h>
#include <dialogs/namingdialog.h>
#include <dialogs/historydialog.h>

#include <tools/optimizer.h>
#include <tools/os.h>
#include <tools/screenshot.h>
#include <tools/screenshotmanager.h>
#include <tools/settings.h>
#include <tools/uploader/customuploader.h>
#include <tools/uploader/uploader.h>

#include <updater/updater.h>

OptionsDialog::OptionsDialog(QWidget *parent) :
    QDialog(parent)
{
    ui.setupUi(this);
    setModal(true);

#if defined(Q_OS_LINUX)
    // KDE-specific style tweaks.
    if (qApp->style()->objectName() == "oxygen") {
        ui.browsePushButton->setMaximumWidth(30);
        ui.namingOptionsButton->setMaximumWidth(30);

        ui.fileGroupBox->setFlat(false);
        ui.startupGroupBox->setFlat(false);
        ui.capturesGroupBox->setFlat(false);
        ui.controlGroupBox->setFlat(false);
        ui.interfaceGroupBox->setFlat(false);
        ui.screenshotsGroupBox->setFlat(false);
        ui.previewGroupBox->setFlat(false);
        ui.updaterGroupBox->setFlat(false);
        ui.historyGroupBox->setFlat(false);
        ui.clipboardGroupBox->setFlat(false);

        ui.optionsTab->layout()->setContentsMargins(0, 0, 6, 0);
        ui.aboutTab->layout()->setContentsMargins(8, 8, 8, 8);
    }
#endif

    setEnabled(false); // We disable the widgets to prevent any user interaction until the settings have loaded.
    QMetaObject::invokeMethod(this, "init"        , Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, "loadSettings", Qt::QueuedConnection);
}

void OptionsDialog::accepted()
{
    if (hotkeyCollision()) {
        QMessageBox::critical(this, tr("Hotkey conflict"), tr("You have assigned the same hotkeys to more than one action."));
        return;
    }

    if (ui.prefixLineEdit->text().contains(QRegularExpression("[?:\\\\/*\"<>|]"))) {
        QMessageBox::critical(this, tr("Filename character error"), tr("The filename can't contain any of the following characters: ? : \\ / * \" < > |"));
        return;
    }

    if (!ui.fileGroupBox->isChecked() && !ui.clipboardCheckBox->isChecked()) {
        QMessageBox::critical(this, tr("Final Destination"), tr("You can't take screenshots unless you enable either file saving or the clipboard."));
        return;
    }

    saveSettings();
    accept();
}

void OptionsDialog::checkUpdatesNow()
{
    Updater updater(ScreenshotManager::instance()->uploader()->network());
    updater.checkWithFeedback();
}

void OptionsDialog::exportSettings()
{
    QString exportFileName = QFileDialog::getSaveFileName(this,
                             tr("Export Settings"),
                             QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QDir::separator() + "config.ini",
                             tr("Lightscreen Settings (*.ini)"));

    if (exportFileName.isEmpty()) {
        return;
    }

    if (QFile::exists(exportFileName)) {
        QFile::remove(exportFileName);
    }

    QSettings exportedSettings(exportFileName, QSettings::IniFormat);

    for (auto key : settings()->allKeys()) {
        exportedSettings.setValue(key, settings()->value(key));
    }
}

void OptionsDialog::importSettings()
{
    QString importFileName = QFileDialog::getOpenFileName(this,
                             tr("Import Settings"),
                             QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                             tr("Lightscreen Settings (*.ini)"));

    QSettings importedSettings(importFileName, QSettings::IniFormat);

    saveSettings();

    for (auto key : importedSettings.allKeys()) {
        if (settings()->contains(key)) {
            settings()->setValue(key, importedSettings.value(key));
        }
    }

    loadSettings();
}

void OptionsDialog::loadSettings()
{
    settings()->sync();
    setUpdatesEnabled(false);

    if (!settings()->contains("file/format")) {
        // If there are no settings, get rid of the cancel button so that the user is forced to save them
        ui.buttonBox->clear();
        ui.buttonBox->addButton(QDialogButtonBox::Ok);
    }

    ui.startupCheckBox->toggle();
    ui.trayCheckBox->toggle();
    ui.previewAutocloseCheckBox->toggle();

    settings()->beginGroup("file");
    ui.formatComboBox->setCurrentIndex(Settings::value(settings(), "format").toInt());
    ui.prefixLineEdit->setText(Settings::value(settings(), "prefix").toString());
    ui.namingComboBox->setCurrentIndex(Settings::value(settings(), "naming").toInt());
    ui.targetLineEdit->setText(Settings::value(settings(), "target", ScreenshotManager::instance()->portableMode()).toString());
    ui.fileGroupBox->setChecked(Settings::value(settings(), "enabled").toBool());
    settings()->endGroup();

    settings()->beginGroup("options");
    ui.startupCheckBox->setChecked(Settings::value(settings(), "startup").toBool());
    ui.startupHideCheckBox->setChecked(Settings::value(settings(), "startupHide").toBool());
    ui.hideCheckBox->setChecked(Settings::value(settings(), "hide").toBool());
    ui.delaySpinBox->setValue(Settings::value(settings(), "delay").toInt());
    flipToggled(Settings::value(settings(), "flip").toBool());
    ui.trayCheckBox->setChecked(Settings::value(settings(), "tray").toBool());
    ui.messageCheckBox->setChecked(Settings::value(settings(), "message").toBool());
    ui.qualitySlider->setValue(Settings::value(settings(), "quality").toInt());
    ui.playSoundCheckBox->setChecked(Settings::value(settings(), "playSound").toBool());
    ui.updaterCheckBox->setChecked(!Settings::value(settings(), "disableUpdater").toBool());
    ui.magnifyCheckBox->setChecked(Settings::value(settings(), "magnify").toBool());

#ifdef Q_OS_WIN
    ui.cursorCheckBox->setChecked(Settings::value(settings(), "cursor").toBool());
#else
    ui.cursorCheckBox->setChecked(Settings::value(settings(), "cursor").toBool());
#endif

    ui.saveAsCheckBox->setChecked(Settings::value(settings(), "saveAs").toBool());
    ui.previewGroupBox->setChecked(Settings::value(settings(), "preview").toBool());
    ui.previewSizeSpinBox->setValue(Settings::value(settings(), "previewSize").toInt());
    ui.previewPositionComboBox->setCurrentIndex(Settings::value(settings(), "previewPosition").toInt());
    ui.previewAutocloseCheckBox->setChecked(Settings::value(settings(), "previewAutoclose").toBool());
    ui.previewAutocloseTimeSpinBox->setValue(Settings::value(settings(), "previewAutocloseTime").toInt());
    ui.previewAutocloseActionComboBox->setCurrentIndex(Settings::value(settings(), "previewAutocloseAction").toInt());
    ui.previewDefaultActionComboBox->setCurrentIndex(Settings::value(settings(), "previewDefaultAction").toInt());
    ui.areaAutocloseCheckBox->setChecked(Settings::value(settings(), "areaAutoclose").toBool());

    // History mode is neat for normal operation but I'll keep it disabled by default on portable mode.
    ui.historyCheckBox->setChecked(Settings::value(settings(), "history", ScreenshotManager::instance()->portableMode()).toBool());

    // Advanced
    ui.clipboardCheckBox->setChecked(Settings::value(settings(), "clipboard").toBool());
    ui.urlClipboardCheckBox->setChecked(Settings::value(settings(), "urlClipboard").toBool());
    ui.optiPngCheckBox->setChecked(Settings::value(settings(), "optimize").toBool());
    ui.closeHideCheckBox->setChecked(Settings::value(settings(), "closeHide").toBool());
    ui.currentMonitorCheckBox->setChecked(Settings::value(settings(), "currentMonitor").toBool());
    ui.replaceCheckBox->setChecked(Settings::value(settings(), "replace").toBool());
    ui.uploadCheckBox->setChecked(Settings::value(settings(), "uploadAuto").toBool());

    // PNG optimizer: the stored choice, or what the automatic choice runs when nothing is stored. A tool that is not
    // found stays selectable, so a choice made before installing it is kept. "optimizer" is written only once the
    // user picks a tool.
    ui.optimizerComboBox->clear();

    for (const QString &tool : Optimizer::toolNames()) {
        QString label = (tool == QLatin1String("optipng")) ? QStringLiteral("OptiPNG") : tool;

        if (Optimizer::program(tool).isEmpty()) {
            label += tr(" (not found)");
        }

        ui.optimizerComboBox->addItem(label, tool);
    }

    const QString automatic = Optimizer::resolveTool(QString());
    QString optimizer = settings()->value("optimizer").toString();

    if (!Optimizer::toolNames().contains(optimizer)) {
        optimizer = automatic.isEmpty() ? Optimizer::toolNames().first() : automatic;
    }

    ui.optimizerComboBox->setCurrentIndex(ui.optimizerComboBox->findData(optimizer));
    mOptimizerPicked = false;

    // With no optimizer at all, the tool choice is moot: the box is hidden, which leaves the hint room to show in full
    // at the dialog's default width (it was cut off; 2026-10-29 Windows pass).
    ui.optimizerComboBox->setVisible(!automatic.isEmpty());

    if (automatic.isEmpty()) {
        ui.optiPngCheckBox->setEnabled(false);
#ifdef Q_OS_WIN
        ui.optiPngLabel->setText(tr("No optimizer found next to lightscreen.exe"));
#else
        ui.optiPngCheckBox->setChecked(false);
        ui.optiPngLabel->setText(tr("Install oxipng or OptiPNG"));
#endif
    }

#ifdef Q_OS_LINUX
    //TODO: Sound cue support on Linux
    ui.playSoundCheckBox->setVisible(false);
    ui.playSoundCheckBox->setChecked(false);

    //TODO: Cursor support on X11
    ui.cursorCheckBox->setVisible(false);
    ui.cursorCheckBox->setChecked(false);
#endif
    settings()->endGroup();

    settings()->beginGroup("actions");
    // This toggle is for the first run
    ui.screenCheckBox->toggle();
    ui.areaCheckBox->toggle();
    ui.windowCheckBox->toggle();
    ui.windowPickerCheckBox->toggle();
    ui.openCheckBox->toggle();
    ui.directoryCheckBox->toggle();

    settings()->beginGroup("screen");
    ui.screenCheckBox->setChecked(Settings::value(settings(), "enabled").toBool());
    ui.screenHotkeyWidget->setHotkey(Settings::value(settings(), "hotkey").toString());
    settings()->endGroup();

    settings()->beginGroup("area");
    ui.areaCheckBox->setChecked(Settings::value(settings(), "enabled").toBool());
    ui.areaHotkeyWidget->setHotkey(Settings::value(settings(), "hotkey").toString());
    settings()->endGroup();

    settings()->beginGroup("window");
    ui.windowCheckBox->setChecked(Settings::value(settings(), "enabled").toBool());
    ui.windowHotkeyWidget->setHotkey(Settings::value(settings(), "hotkey").toString());
    settings()->endGroup();

    settings()->beginGroup("windowPicker");
    ui.windowPickerCheckBox->setChecked(Settings::value(settings(), "enabled").toBool());
    ui.windowPickerHotkeyWidget->setHotkey(Settings::value(settings(), "hotkey").toString());
    settings()->endGroup();

    settings()->beginGroup("open");
    ui.openCheckBox->setChecked(Settings::value(settings(), "enabled").toBool());
    ui.openHotkeyWidget->setHotkey(Settings::value(settings(), "hotkey").toString());
    settings()->endGroup();

    settings()->beginGroup("directory");
    ui.directoryCheckBox->setChecked(Settings::value(settings(), "enabled").toBool());
    ui.directoryHotkeyWidget->setHotkey(Settings::value(settings(), "hotkey").toString());
    settings()->endGroup();
    settings()->endGroup();

    settings()->beginGroup("upload");
    const int service = Settings::value(settings(), "service").toInt();
    ui.uploadServiceComboBox->setCurrentIndex((service >= 0 && service < ui.uploadServiceComboBox->count()) ? service : 0);

    settings()->endGroup();

    CustomUploader::migrateSettings(settings());
    ui.uploaderOptions->setUploaderSettings(ImageUploader::loadSettings(settings(), "custom"));

    QTimer::singleShot(0, this, &OptionsDialog::updatePreview);

    setEnabled(true);
    setUpdatesEnabled(true);
}

void OptionsDialog::openUrl(QString url)
{
    if (url == "#aboutqt") {
        qApp->aboutQt();
    } else {
        QDesktopServices::openUrl(QUrl(url));
    }
}

void OptionsDialog::saveSettings()
{
    // A stored index the combo box does not list leaves it with no current item (-1); keep the stored value then
    // instead of overwriting it with -1 (known-bugs.md, bug 19).
    const auto setIndexUnlessUnknown = [this](const QString &key, const QComboBox *comboBox) {
        if (comboBox->currentIndex() >= 0) {
            settings()->setValue(key, comboBox->currentIndex());
        }
    };

    settings()->beginGroup("file");
    setIndexUnlessUnknown("format", ui.formatComboBox);
    settings()->setValue("prefix", ui.prefixLineEdit->text());
    setIndexUnlessUnknown("naming", ui.namingComboBox);
    settings()->setValue("target", ui.targetLineEdit->text());
    settings()->setValue("enabled", ui.fileGroupBox->isChecked());
    settings()->endGroup();

    settings()->beginGroup("options");
    settings()->setValue("startup", ui.startupCheckBox->isChecked());
    settings()->setValue("startupHide", ui.startupHideCheckBox->isChecked());
    settings()->setValue("hide", ui.hideCheckBox->isChecked());
    settings()->setValue("delay", ui.delaySpinBox->value());
    settings()->setValue("tray", ui.trayCheckBox->isChecked());
    settings()->setValue("message", ui.messageCheckBox->isChecked());
    settings()->setValue("quality", ui.qualitySlider->value());
    settings()->setValue("playSound", ui.playSoundCheckBox->isChecked());
    // This settings is inverted because the first iteration of the Updater did not have a settings but instead relied on the messagebox choice of the user.
    settings()->setValue("disableUpdater", !ui.updaterCheckBox->isChecked());
    settings()->setValue("magnify", ui.magnifyCheckBox->isChecked());
    settings()->setValue("cursor", ui.cursorCheckBox->isChecked());
    settings()->setValue("saveAs", ui.saveAsCheckBox->isChecked());
    settings()->setValue("preview", ui.previewGroupBox->isChecked());
    settings()->setValue("previewSize", ui.previewSizeSpinBox->value());
    setIndexUnlessUnknown("previewPosition", ui.previewPositionComboBox);
    settings()->setValue("previewAutoclose", ui.previewAutocloseCheckBox->isChecked());
    settings()->setValue("previewAutocloseTime", ui.previewAutocloseTimeSpinBox->value());
    settings()->setValue("previewAutocloseAction", ui.previewAutocloseActionComboBox->currentIndex());
    settings()->setValue("previewDefaultAction", ui.previewDefaultActionComboBox->currentIndex());
    settings()->setValue("areaAutoclose", ui.areaAutocloseCheckBox->isChecked());
    settings()->setValue("history", ui.historyCheckBox->isChecked());

    // Advanced
    settings()->setValue("closeHide", ui.closeHideCheckBox->isChecked());
    settings()->setValue("clipboard", ui.clipboardCheckBox->isChecked());
    settings()->setValue("urlClipboard", ui.urlClipboardCheckBox->isChecked());
    settings()->setValue("optimize", ui.optiPngCheckBox->isChecked());

    if (mOptimizerPicked || settings()->contains("optimizer")) {
        settings()->setValue("optimizer", ui.optimizerComboBox->currentData().toString());
    }

    settings()->setValue("currentMonitor", ui.currentMonitorCheckBox->isChecked());
    settings()->setValue("replace", ui.replaceCheckBox->isChecked());

    //Upload
    settings()->setValue("uploadAuto", ui.uploadCheckBox->isChecked());
    settings()->endGroup();

    settings()->beginGroup("actions");
    settings()->beginGroup("screen");
    settings()->setValue("enabled", ui.screenCheckBox->isChecked());
    settings()->setValue("hotkey" , ui.screenHotkeyWidget->hotkey());
    settings()->endGroup();

    settings()->beginGroup("area");
    settings()->setValue("enabled", ui.areaCheckBox->isChecked());
    settings()->setValue("hotkey" , ui.areaHotkeyWidget->hotkey());
    settings()->endGroup();

    settings()->beginGroup("window");
    settings()->setValue("enabled", ui.windowCheckBox->isChecked());
    settings()->setValue("hotkey" , ui.windowHotkeyWidget->hotkey());
    settings()->endGroup();

    settings()->beginGroup("windowPicker");
    settings()->setValue("enabled", ui.windowPickerCheckBox->isChecked());
    settings()->setValue("hotkey" , ui.windowPickerHotkeyWidget->hotkey());
    settings()->endGroup();

    settings()->beginGroup("open");
    settings()->setValue("enabled", ui.openCheckBox->isChecked());
    settings()->setValue("hotkey" , ui.openHotkeyWidget->hotkey());
    settings()->endGroup();

    settings()->beginGroup("directory");
    settings()->setValue("enabled", ui.directoryCheckBox->isChecked());
    settings()->setValue("hotkey" , ui.directoryHotkeyWidget->hotkey());
    settings()->endGroup();

    settings()->endGroup();

    settings()->beginGroup("upload");
    settings()->setValue("service", ui.uploadServiceComboBox->currentIndex());

    settings()->endGroup();

    ImageUploader::saveSettings(settings(), "custom", ui.uploaderOptions->uploaderSettings());
}

void OptionsDialog::updatePreview()
{
    Screenshot::NamingOptions options;

    options.naming       = (Screenshot::Naming) ui.namingComboBox->currentIndex();
    options.flip         = Settings::value(settings(), "options/flip").toBool();
    options.leadingZeros = Settings::value(settings(), "options/naming/leadingZeros").toInt();
    options.dateFormat   = Settings::value(settings(), "options/naming/dateFormat").toString();

    if (ui.fileGroupBox->isChecked()) { // Only change the naming button when file options are enabled.
        ui.namingOptionsButton->setDisabled((options.naming == Screenshot::Empty));
    }

    QString preview = Screenshot::getName(options,
                                          ui.prefixLineEdit->text(),
                                          QDir(ui.targetLineEdit->text()));

    preview = QString("%1.%2").arg(preview).arg(ui.formatComboBox->currentText().toLower());

    if (preview.length() >= 40) {
        preview.truncate(37);
        preview.append("...");
    }

    ui.previewLabel->setText(preview);

    ui.qualitySlider->setEnabled(ui.formatComboBox->currentText() != "PNG");
    ui.qualityLabel->setEnabled(ui.qualitySlider->isEnabled());
}

void OptionsDialog::viewHistory()
{
    HistoryDialog historyDialog(this);
    historyDialog.exec();
}

//

bool OptionsDialog::event(QEvent *event)
{
    if (event->type() == QEvent::Close || event->type() == QEvent::Hide) {
        settings()->setValue("geometry/optionsDialog", saveGeometry());

        if (!settings()->contains("file/format")) {
            // I'm afraid I can't let you do that, Dave.
            event->ignore();
            return false;
        }
    } else if (event->type() == QEvent::Show) {
        if (settings()->contains("geometry/optionsDialog")) {
            restoreGeometry(settings()->value("geometry/optionsDialog").toByteArray());
        } else {
            move(screen()->geometry().center() - rect().center());
        }
    }

    return QDialog::event(event);
}

#ifdef Q_OS_WIN
// Qt does not send the print screen key as a regular QKeyPress event, so we must use the Windows API
bool OptionsDialog::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType == "windows_generic_MSG") {
        MSG *m = static_cast<MSG *>(message);

        if ((m->message == WM_KEYUP || m->message == WM_SYSKEYUP) && m->wParam == VK_SNAPSHOT) {
            qApp->postEvent(qApp->focusWidget(), new QKeyEvent(QEvent::KeyPress, Qt::Key_Print,  qApp->queryKeyboardModifiers()));
        }
    }

    return QDialog::nativeEvent(eventType, message, result);
}
#endif

//

void OptionsDialog::browse()
{
    QString fileName = QFileDialog::getExistingDirectory(this,
                       tr("Select where you want to save the screenshots"),
                       ui.targetLineEdit->text());

    if (fileName.isEmpty()) {
        return;
    }

    ui.targetLineEdit->setText(fileName);
    updatePreview();
}

void OptionsDialog::dialogButtonClicked(QAbstractButton *button)
{
    if (ui.buttonBox->buttonRole(button) == QDialogButtonBox::ResetRole) {
        QPushButton *pb = qobject_cast<QPushButton *>(button);
        pb->showMenu();
    }
}

void OptionsDialog::restoreDefaults()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle(tr("Lightscreen Rd - Restore Default Options"));
    msgBox.setText(tr("Restoring the default options will cause you to lose all of your current configuration."));
    msgBox.setIcon(QMessageBox::Warning);

    msgBox.addButton(tr("Restore"), QMessageBox::ActionRole);
    QPushButton *dontRestoreButton = msgBox.addButton(tr("Don't Restore"), QMessageBox::ActionRole);

    msgBox.setDefaultButton(dontRestoreButton);
    msgBox.exec();

    if (msgBox.clickedButton() == dontRestoreButton) {
        return;
    }

    settings()->clear();
    loadSettings();
}

void OptionsDialog::flipToggled(bool checked)
{
    setUpdatesEnabled(false);

    ui.filenameLayout->removeWidget(ui.prefixLineEdit);
    ui.filenameLayout->removeWidget(ui.namingComboBox);

    if (checked) {
        ui.filenameLayout->addWidget(ui.namingComboBox);
        ui.filenameLayout->addWidget(ui.prefixLineEdit);
    } else {
        ui.filenameLayout->addWidget(ui.prefixLineEdit);
        ui.filenameLayout->addWidget(ui.namingComboBox);
    }

    if (ui.prefixLineEdit->text() == tr("screenshot.")
            && checked) {
        ui.prefixLineEdit->setText(tr(".screenshot"));
    }

    if (ui.prefixLineEdit->text() == tr(".screenshot")
            && !checked) {
        ui.prefixLineEdit->setText(tr("screenshot."));
    }

    setUpdatesEnabled(true); // Avoids flicker
}

void OptionsDialog::init()
{
    // Make the scroll area share the Tab Widget background color
    ui.optionsScrollArea->setStyleSheet("QScrollArea { background-color: transparent; } #scrollAreaWidgetContents { background-color: transparent; }");

    ui.browsePushButton->setIcon(os::icon("folder"));
    ui.namingOptionsButton->setIcon(os::icon("configure"));

    // Export/Import menu .
    QMenu *optionsMenu = new QMenu(tr("Options"));

    QAction *exportAction = new QAction(tr("&Export.."), optionsMenu);
    connect(exportAction, &QAction::triggered, this, &OptionsDialog::exportSettings);
    connect(ui.optimizerComboBox, &QComboBox::activated, this, [this] {
        mOptimizerPicked = true;
    });

    QAction *importAction = new QAction(tr("&Import.."), optionsMenu);
    connect(importAction, &QAction::triggered, this, &OptionsDialog::importSettings);

    QAction *restoreAction = new QAction(tr("&Restore Defaults"), optionsMenu);
    connect(restoreAction, &QAction::triggered, this, &OptionsDialog::restoreDefaults);

    optionsMenu->addAction(exportAction);
    optionsMenu->addAction(importAction);
    optionsMenu->addSeparator();
    optionsMenu->addAction(restoreAction);

    QPushButton *optionsButton = new QPushButton(tr("Options"), this);
    optionsButton->setMenu(optionsMenu);
    ui.buttonBox->addButton(optionsButton, QDialogButtonBox::ResetRole);

    // Autocomplete for the directory: the subfolders of the folder typed so far, read as you type. (QFileSystemModel,
    // which replaced Qt 5's QDirModel in M04, loads folders in the background and QCompleter does not retry once they
    // have loaded, so it suggested nothing; found in the Windows QA pass.)
    QCompleter *completer = new QCompleter(this);
    QStringListModel *folders = new QStringListModel(completer);
    completer->setModel(folders);
#ifdef Q_OS_WIN
    completer->setCaseSensitivity(Qt::CaseInsensitive);
#endif
    ui.targetLineEdit->setCompleter(completer);

    connect(ui.targetLineEdit, &QLineEdit::textEdited, completer, [completer, folders](const QString &text) {
        const QString path = QDir::fromNativeSeparators(text);
        const QString parent = path.left(path.lastIndexOf('/') + 1);
        QStringList paths;

        if (!parent.isEmpty()) {
            for (const QString &name : QDir(parent).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                paths << QDir::toNativeSeparators(parent + name);
            }
        }

        if (paths != folders->stringList()) {
            folders->setStringList(paths);
        }

        completer->setCompletionPrefix(text);
        completer->complete();
    });

    // HotkeyWidget icons.
    ui.screenHotkeyWidget->setIcon(os::icon("screen"));
    ui.windowHotkeyWidget->setIcon(os::icon("window"));
    ui.windowPickerHotkeyWidget->setIcon(os::icon("pickWindow"));
    ui.areaHotkeyWidget->setIcon(os::icon("area"));
    ui.openHotkeyWidget->setIcon(QIcon(":/icons/lightscreen.small"));
    ui.directoryHotkeyWidget->setIcon(os::icon("folder"));

    // Version
    ui.versionLabel->setText(tr("Version %1").arg(qApp->applicationVersion()));
    // The fork's link comes from the repository the update check reads; the .ui holds the link to the original.
    ui.linksLabel->setText(tr("<a href=\"https://github.com/%1\">Lightscreen Rd on GitHub</a> · %2").arg(Updater::repository(), ui.linksLabel->text()));

    ui.uploadSslWarningLabel->setVisible(false);

    // Run the SSL check in another thread (slows down dialog startup considerably).
    auto futureWatcher = new QFutureWatcher<bool>(this);
    connect(futureWatcher, &QFutureWatcher<bool>::finished, futureWatcher, &QFutureWatcher<bool>::deleteLater);
    connect(futureWatcher, &QFutureWatcher<bool>::finished, this, [&, futureWatcher] {
        ui.uploadSslWarningLabel->setVisible(!futureWatcher->future().result());
    });

    futureWatcher->setFuture(QtConcurrent::run([]() -> bool { return QSslSocket::supportsSsl(); }));

    //
    // Connections
    //

    connect(ui.buttonBox, &QDialogButtonBox::clicked     , this, &OptionsDialog::dialogButtonClicked);
    connect(ui.buttonBox, &QDialogButtonBox::accepted    , this, &OptionsDialog::accepted);
    connect(ui.namingOptionsButton, &QPushButton::clicked, this, &OptionsDialog::namingOptions);

    connect(ui.prefixLineEdit, &QLineEdit::textEdited, this, &OptionsDialog::updatePreview);
    connect(ui.formatComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &OptionsDialog::updatePreview);
    connect(ui.namingComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &OptionsDialog::updatePreview);

    connect(ui.browsePushButton       , &QPushButton::clicked, this, &OptionsDialog::browse);
    connect(ui.checkUpdatesPushButton , &QPushButton::clicked, this, &OptionsDialog::checkUpdatesNow);
    connect(ui.historyPushButton      , &QPushButton::clicked, this, &OptionsDialog::viewHistory);

    connect(ui.windowPickerCheckBox, &QCheckBox::toggled, ui.windowPickerHotkeyWidget, &HotkeyWidget::setEnabled);

    connect(ui.screenCheckBox      , &QCheckBox::toggled, ui.screenHotkeyWidget   , &HotkeyWidget::setEnabled);
    connect(ui.areaCheckBox        , &QCheckBox::toggled, ui.areaHotkeyWidget     , &HotkeyWidget::setEnabled);
    connect(ui.windowCheckBox      , &QCheckBox::toggled, ui.windowHotkeyWidget   , &HotkeyWidget::setEnabled);
    connect(ui.openCheckBox        , &QCheckBox::toggled, ui.openHotkeyWidget     , &HotkeyWidget::setEnabled);
    connect(ui.directoryCheckBox   , &QCheckBox::toggled, ui.directoryHotkeyWidget, &HotkeyWidget::setEnabled);

    // "Save as" disables the file target input field.
    connect(ui.saveAsCheckBox, &QCheckBox::toggled, [&](bool checked) {
        ui.targetLineEdit->setDisabled(checked);
        ui.browsePushButton->setDisabled(checked);
        ui.directoryLabel->setDisabled(checked);
    });

    connect(ui.qualitySlider, static_cast<void (QSlider::*)(int)>(&QSlider::valueChanged), ui.qualityValueLabel, static_cast<void (QLabel::*)(int)>(&QLabel::setNum));
    connect(ui.startupCheckBox, &QCheckBox::toggled   , ui.startupHideCheckBox, &QCheckBox::setEnabled);
    connect(ui.trayCheckBox   , &QCheckBox::toggled   , ui.messageCheckBox    , &QCheckBox::setEnabled);

    // Auto-upload disables the default action button in the previews.
    connect(ui.uploadCheckBox, &QCheckBox::toggled, [&](bool checked) {
        ui.previewDefaultActionLabel->setDisabled(checked);
        ui.previewDefaultActionComboBox->setDisabled(checked);
        ui.directoryHotkeyWidget->setEnabled(checked);
    });

    auto conflictWarning = [](bool fullConflict, QWidget *w) {
        if (fullConflict) {
            QToolTip::showText(QCursor::pos(), tr("<font color=\"darkRed\">This setting conflicts with the Screenshot Clipboard setting, which has been disabled.</font>"), w);
        } else {
            QToolTip::showText(QCursor::pos(), tr("<b>This setting might conflict with the Screenshot Clipboard setting!</b>"), w);
        }
    };

    connect(ui.uploadCheckBox, &QCheckBox::toggled, [&](bool checked) {
        if (ui.urlClipboardCheckBox->isChecked() && ui.clipboardCheckBox->isChecked()) {
            ui.clipboardGroupBox->setDisabled(checked);

            if (checked) {
                conflictWarning(true, ui.uploadCheckBox);
            }
        }
    });

    connect(ui.urlClipboardCheckBox, &QCheckBox::toggled, [&](bool checked) {
        if (ui.uploadCheckBox->isChecked() && ui.clipboardCheckBox->isChecked()) {
            ui.clipboardGroupBox->setDisabled(checked);

            if (checked) {
                conflictWarning(true, ui.urlClipboardCheckBox);
            }
        } else if (ui.clipboardCheckBox->isChecked()) {
            conflictWarning(false, ui.urlClipboardCheckBox);
        }
    });

    connect(ui.mainLabel ,           &QLabel::linkActivated, this, &OptionsDialog::openUrl);
    connect(ui.licenseAboutLabel,    &QLabel::linkActivated, this, &OptionsDialog::openUrl);
    connect(ui.linksLabel,           &QLabel::linkActivated, this, &OptionsDialog::openUrl);
    connect(ui.uploadSslWarningLabel,&QLabel::linkActivated, this, &OptionsDialog::openUrl);
}

void OptionsDialog::namingOptions()
{
    NamingDialog namingDialog((Screenshot::Naming) ui.namingComboBox->currentIndex());

    namingDialog.exec();
    flipToggled(Settings::value(settings(), "options/flip").toBool());
    updatePreview();
}

//

bool OptionsDialog::hotkeyCollision()
{
    // Check for hotkey collision (there's probably a better way to do this...=)

    if (ui.screenCheckBox->isChecked()) {
        if (ui.screenHotkeyWidget->hotkey() == ui.areaHotkeyWidget->hotkey()
                && ui.areaCheckBox->isChecked()) {
            return true;
        }

        if (ui.screenHotkeyWidget->hotkey() == ui.windowHotkeyWidget->hotkey()
                && ui.windowCheckBox->isChecked()) {
            return true;
        }

        if (ui.screenHotkeyWidget->hotkey() == ui.windowPickerHotkeyWidget->hotkey()
                && ui.windowPickerCheckBox->isChecked()) {
            return true;
        }

        if (ui.screenHotkeyWidget->hotkey() == ui.openHotkeyWidget->hotkey()
                && ui.openCheckBox->isChecked()) {
            return true;
        }

        if (ui.screenHotkeyWidget->hotkey() == ui.directoryHotkeyWidget->hotkey()
                && ui.directoryCheckBox->isChecked()) {
            return true;
        }
    }

    if (ui.areaCheckBox->isChecked()) {
        if (ui.areaHotkeyWidget->hotkey() == ui.screenHotkeyWidget->hotkey()
                && ui.screenCheckBox->isChecked()) {
            return true;
        }

        if (ui.areaHotkeyWidget->hotkey() == ui.windowHotkeyWidget->hotkey()
                && ui.windowCheckBox->isChecked()) {
            return true;
        }

        if (ui.areaHotkeyWidget->hotkey() == ui.windowPickerHotkeyWidget->hotkey()
                && ui.windowPickerCheckBox->isChecked()) {
            return true;
        }

        if (ui.areaHotkeyWidget->hotkey() == ui.openHotkeyWidget->hotkey()
                && ui.openCheckBox->isChecked()) {
            return true;
        }

        if (ui.areaHotkeyWidget->hotkey() == ui.directoryHotkeyWidget->hotkey()
                && ui.directoryCheckBox->isChecked()) {
            return true;
        }
    }

    if (ui.windowCheckBox->isChecked()) {
        if (ui.windowHotkeyWidget->hotkey() == ui.screenHotkeyWidget->hotkey()
                && ui.screenCheckBox->isChecked()) {
            return true;
        }

        if (ui.windowHotkeyWidget->hotkey() == ui.areaHotkeyWidget->hotkey()
                && ui.areaCheckBox->isChecked()) {
            return true;
        }

        if (ui.windowHotkeyWidget->hotkey() == ui.windowPickerHotkeyWidget->hotkey()
                && ui.windowPickerCheckBox->isChecked()) {
            return true;
        }

        if (ui.windowHotkeyWidget->hotkey() == ui.openHotkeyWidget->hotkey()
                && ui.openCheckBox->isChecked()) {
            return true;
        }

        if (ui.windowHotkeyWidget->hotkey() == ui.directoryHotkeyWidget->hotkey()
                && ui.directoryCheckBox->isChecked()) {
            return true;
        }
    }

    if (ui.openCheckBox->isChecked()) {
        if (ui.openHotkeyWidget->hotkey() == ui.screenHotkeyWidget->hotkey()
                && ui.screenCheckBox->isChecked()) {
            return true;
        }

        if (ui.openHotkeyWidget->hotkey() == ui.areaHotkeyWidget->hotkey()
                && ui.areaCheckBox->isChecked()) {
            return true;
        }

        if (ui.openHotkeyWidget->hotkey() == ui.windowPickerHotkeyWidget->hotkey()
                && ui.windowPickerCheckBox->isChecked()) {
            return true;
        }

        if (ui.openHotkeyWidget->hotkey() == ui.windowHotkeyWidget->hotkey()
                && ui.windowCheckBox->isChecked()) {
            return true;
        }

        if (ui.openHotkeyWidget->hotkey() == ui.directoryHotkeyWidget->hotkey()
                && ui.directoryCheckBox->isChecked()) {
            return true;
        }
    }

    if (ui.directoryCheckBox->isChecked()) {
        if (ui.directoryHotkeyWidget->hotkey() == ui.screenHotkeyWidget->hotkey()
                && ui.screenCheckBox->isChecked()) {
            return true;
        }

        if (ui.directoryHotkeyWidget->hotkey() == ui.areaHotkeyWidget->hotkey()
                && ui.areaCheckBox->isChecked()) {
            return true;
        }

        if (ui.directoryHotkeyWidget->hotkey() == ui.windowPickerHotkeyWidget->hotkey()
                && ui.windowPickerCheckBox->isChecked()) {
            return true;
        }

        if (ui.directoryHotkeyWidget->hotkey() == ui.windowHotkeyWidget->hotkey()
                && ui.windowCheckBox->isChecked()) {
            return true;
        }

        if (ui.directoryHotkeyWidget->hotkey() == ui.openHotkeyWidget->hotkey()
                && ui.openCheckBox->isChecked()) {
            return true;
        }
    }

    return false;
}

QSettings *OptionsDialog::settings() const
{
    return ScreenshotManager::instance()->settings();
}
