// Stage 4 safety net, session 1: the settings contract (docs/modernization-plan.md, section 6, item 1).
//
// Records what OptionsDialog writes on empty settings (defaults.golden), what survives a save/reload round trip
// with every widget changed, and what LightscreenWindow::screenshotAction reads from the same settings. These tests
// describe current behaviour, quirks included; a change that moves any of them must update this file on purpose.
#include <QtTest>
#include <algorithm>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QGroupBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>

#include <dialogs/namingdialog.h>
#include <dialogs/optionsdialog.h>
#include <lightscreenwindow.h>
#include <tools/optimizer.h>
#include <tools/os.h>
#include <tools/screenshot.h>
#include <tools/screenshotmanager.h>
#include <tools/settings.h>
#include <tools/uploader/uploader.h>
#include <widgets/hotkeywidget.h>

class tst_Settings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void dialogDefaults();
    void applicationIdentity();
    void defaultsAgreeWithDialog();
    void dialogRoundTrip();
    void dialogOutOfRangeIndexes();
    void dialogLegacyImgurSettings();
    void dialogUploaderHeaders();
    void dialogOptimizerChoice();
    void windowReadsEmptySettings();
    void windowReadsDialogDefaults();
    void windowCachesOptions();
    void windowPngQualityIsForced();
    void targetFolderCompletes();
    void dateFormatRejectsFileNameCharacters();

private:
    QStringList dump() const;
    QString placeholders(QString value) const;
    QStringList golden(const QString &fileName) const;
    QStringList describe(const Screenshot::Options &options) const;
    bool probeWindow(LightscreenWindow &window, Screenshot::Options *options);
    void setChecked(OptionsDialog &dialog, const char *name, bool checked);

    QTemporaryDir mHome;
    QScopedPointer<QTemporaryDir> mDir;
    QScopedPointer<QSettings> mSettings;
    Uploader *mUploader = nullptr; // owned by mManager
    QScopedPointer<QNetworkAccessManager> mNetwork;
    QScopedPointer<ScreenshotManager> mManager;
};

// Compares two key=value lists and fails with every difference listed, not just the first. Uses std::equal, not
// QStringList::operator==: with Qt 5.15.2 and current MSVC (CI's windows-latest), QList::operator== does not compile
// (it calls stdext::make_checked_array_iterator, which the newer MSVC standard library removed).
#define COMPARE_LINES(actual, expected)                                                                     \
    do {                                                                                                    \
        const QStringList a_ = (actual), e_ = (expected);                                                   \
        if (a_.size() != e_.size() || !std::equal(a_.cbegin(), a_.cend(), e_.cbegin())) {                \
            QStringList diff_;                                                                              \
            for (const QString &line : e_) if (!a_.contains(line)) diff_ << "- " + line;                    \
            for (const QString &line : a_) if (!e_.contains(line)) diff_ << "+ " + line;                    \
            QFAIL(qPrintable("Settings differ (- expected, + actual):\n" + diff_.join('\n')));              \
        }                                                                                                   \
    } while (false)

void tst_Settings::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QVERIFY(mHome.isValid());

#ifdef Q_OS_LINUX
    // LightscreenWindow::applySettings calls os::setStartup, which removes ~/.config/autostart/lightscreen.desktop,
    // and the dialog's default target is under ~/Documents. Point HOME at a temporary directory for both.
    qputenv("HOME", mHome.path().toLocal8Bit());
#endif
}

void tst_Settings::init()
{
    mDir.reset(new QTemporaryDir);
    QVERIFY(mDir->isValid());

    mSettings.reset(new QSettings(mDir->filePath("settings.ini"), QSettings::IniFormat));
    mManager.reset(new ScreenshotManager(mSettings.data(), mDir->path() + QDir::separator()));
    ScreenshotManager::setInstance(mManager.data());

    mNetwork.reset(new QNetworkAccessManager);
    mUploader = mManager->uploader();
    mUploader->setNetwork(mNetwork.data());
}

void tst_Settings::cleanup()
{
    ScreenshotManager::setInstance(nullptr);
    mManager.reset();
    mUploader = nullptr;
    mSettings.reset();
    mNetwork.reset();
    mDir.reset();
}

// The settings file as written to disk, one "key=value" per line, sorted, with machine-specific paths replaced.
QStringList tst_Settings::dump() const
{
    mSettings->sync();

    QSettings file(mSettings->fileName(), QSettings::IniFormat);
    QStringList lines;

    for (const QString &key : file.allKeys()) {
        lines << key + "=" + placeholders(file.value(key).toString());
    }

    lines.sort();
    return lines;
}

QString tst_Settings::placeholders(QString value) const
{
    // Longest first: the temporary directory may live under HOME.
    value.replace(QDir::toNativeSeparators(mDir->path()), "<tmp>");
    value.replace(mDir->path(), "<tmp>");
    value.replace(os::getDocumentsPath(), "<documents>");
    value.replace(QDir::toNativeSeparators(QDir::currentPath()), "<cwd>");
    value.replace(QDir::currentPath(), "<cwd>");

    if (value.startsWith('<')) {
        value.replace('\\', '/');
    }

    return value;
}

// Reads a golden list next to this file. Lines starting with "linux:" or "windows:" apply only on that platform
// and replace the common line for the same key. Blank lines and lines starting with "#" are ignored.
QStringList tst_Settings::golden(const QString &fileName) const
{
    const QString path = QFINDTESTDATA(fileName);
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open golden file" << fileName;
        return {};
    }

#ifdef Q_OS_WIN
    const QString platform = "windows:";
#else
    const QString platform = "linux:";
#endif

    QMap<QString, QString> common, specific;

    for (QString line : QString::fromUtf8(file.readAll()).split('\n')) {
        line = line.trimmed();

        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        QMap<QString, QString> *target = &common;

        if (line.startsWith("linux:") || line.startsWith("windows:")) {
            if (!line.startsWith(platform)) {
                continue;
            }

            line.remove(0, platform.size());
            target = &specific;
        }

        target->insert(line.section('=', 0, 0), line);
    }

    for (auto it = specific.cbegin(); it != specific.cend(); ++it) {
        common.insert(it.key(), it.value());
    }

    QStringList lines = common.values();
    lines.sort();
    return lines;
}

QStringList tst_Settings::describe(const Screenshot::Options &o) const
{
    auto b = [](bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); };

    return {
        QString("animations=%1").arg(b(o.animations)),
        QString("clipboard=%1").arg(b(o.clipboard)),
        QString("currentMonitor=%1").arg(b(o.currentMonitor)),
        QString("cursor=%1").arg(b(o.cursor)),
        QString("directory=%1").arg(placeholders(o.directory.path())),
        QString("file=%1").arg(b(o.file)),
        QString("format=%1").arg(o.format),
        QString("magnify=%1").arg(b(o.magnify)),
        QString("naming.dateFormat=%1").arg(o.namingOptions.dateFormat),
        QString("naming.flip=%1").arg(b(o.namingOptions.flip)),
        QString("naming.leadingZeros=%1").arg(o.namingOptions.leadingZeros),
        QString("naming.naming=%1").arg(o.namingOptions.naming),
        QString("optimize=%1").arg(b(o.optimize)),
        QString("prefix=%1").arg(o.prefix),
        QString("preview=%1").arg(b(o.preview)),
        QString("quality=%1").arg(o.quality),
        QString("replace=%1").arg(b(o.replace)),
        QString("saveAs=%1").arg(b(o.saveAs)),
        QString("upload=%1").arg(b(o.upload)),
        QString("uploadService=%1").arg(o.uploadService),
        QString("urlClipboard=%1").arg(b(o.urlClipboard)),
    };
}

// Runs LightscreenWindow::screenshotAction and captures the Screenshot::Options it hands to ScreenshotManager.
// Mode 5 is not an enumerator, so Screenshot::take() captures nothing and cancels straight away; that path still
// emits ScreenshotManager::windowCleanup with the screenshot's options. Note those are the options after the
// Screenshot constructor, which forces quality to 80 for PNG.
bool tst_Settings::probeWindow(LightscreenWindow &window, Screenshot::Options *options)
{
    bool captured = false;
    auto connection = connect(mManager.data(), &ScreenshotManager::windowCleanup, this,
                              [&](const Screenshot::Options &o) { *options = o; captured = true; });

    window.screenshotAction(static_cast<Screenshot::Mode>(5));

    // screenshotAction always delays by at least 400 ms before calling ScreenshotManager::take.
    QDeadlineTimer deadline(5000);
    while (!captured && !deadline.hasExpired()) {
        QTest::qWait(20);
    }

    disconnect(connection);
    return captured;
}

void tst_Settings::setChecked(OptionsDialog &dialog, const char *name, bool checked)
{
    QAbstractButton *button = dialog.findChild<QAbstractButton *>(name);

    if (button) {
        button->setChecked(checked);
    } else {
        QGroupBox *group = dialog.findChild<QGroupBox *>(name);
        QVERIFY2(group, name);
        group->setChecked(checked);
    }
}

// OptionsDialog on empty settings, saved without changes: every key it writes, with its default.
void tst_Settings::dialogDefaults()
{
    OptionsDialog dialog;
    QTRY_VERIFY(dialog.isEnabled()); // init() and loadSettings() are queued from the constructor.

    dialog.saveSettings();

    if (qEnvironmentVariableIsSet("LIGHTSCREEN_PRINT_GOLDEN")) {
        qInfo().noquote() << "\n" + dump().join('\n');
    }

    COMPARE_LINES(dump(), golden("defaults.golden"));
}

// The rename (M16) changes only the display name. The organization and application names decide where QSettings keeps
// the settings (and the history next to them) and key the single-instance check, so they must stay as upstream had
// them, or an upgrade starts from empty settings.
void tst_Settings::applicationIdentity()
{
    const QString organization = QCoreApplication::organizationName();
    const QString application = QCoreApplication::applicationName();
    const QString display = QGuiApplication::applicationDisplayName();

    Settings::applyApplicationIdentity();

    QCOMPARE(QCoreApplication::organizationName(), QStringLiteral("K"));
    QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("Lightscreen"));
    QCOMPARE(QGuiApplication::applicationDisplayName(), QStringLiteral("Lightscreen Rd"));

    QCoreApplication::setOrganizationName(organization);
    QCoreApplication::setApplicationName(application);
    QGuiApplication::setApplicationDisplayName(display);
}

// One default per key (S06): every option the dialog writes on empty settings is listed in tools/settings with the
// value the dialog wrote, except the two the dialog forces off on Linux (no cursor or sound support there). Keys the
// table leaves out on purpose: the uploader's upload/custom/*, whose defaults live in CustomUploader.
void tst_Settings::defaultsAgreeWithDialog()
{
    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        dialog.saveSettings();
    }

    const QStringList keys = Settings::keys();
    QStringList mismatches;

    for (const QString &line : dump()) {
        const QString key = line.section('=', 0, 0);
        const QString written = line.section('=', 1);

        if (key.startsWith("upload/custom/")) {
            continue;
        }

#ifdef Q_OS_LINUX
        if (key == "options/cursor" || key == "options/playSound") {
            continue;
        }
#endif

        if (!keys.contains(key)) {
            mismatches << key + " is not in Settings::keys()";
        } else if (placeholders(Settings::defaultValue(key).toString()) != written) {
            mismatches << QString("%1: dialog wrote %2, default is %3").arg(key, written, Settings::defaultValue(key).toString());
        }
    }

    QVERIFY2(mismatches.isEmpty(), qPrintable(mismatches.join('\n')));
}

// Every widget set to a non-default value, saved, loaded into a second dialog, saved again.
void tst_Settings::dialogRoundTrip()
{
    const QString target = mDir->filePath("shots");

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());

        dialog.findChild<QComboBox *>("formatComboBox")->setCurrentIndex(2);
        dialog.findChild<QLineEdit *>("prefixLineEdit")->setText("shot-");
        dialog.findChild<QComboBox *>("namingComboBox")->setCurrentIndex(2);
        dialog.findChild<QLineEdit *>("targetLineEdit")->setText(target);
        setChecked(dialog, "fileGroupBox", false);

        setChecked(dialog, "startupCheckBox", true);
        setChecked(dialog, "startupHideCheckBox", false);
        setChecked(dialog, "hideCheckBox", false);
        dialog.findChild<QSpinBox *>("delaySpinBox")->setValue(7);
        setChecked(dialog, "trayCheckBox", false);
        setChecked(dialog, "messageCheckBox", false);
        dialog.findChild<QSlider *>("qualitySlider")->setValue(42);
        setChecked(dialog, "playSoundCheckBox", true);
        setChecked(dialog, "updaterCheckBox", false);
        setChecked(dialog, "magnifyCheckBox", true);
#ifdef Q_OS_WIN
        setChecked(dialog, "cursorCheckBox", false); // Default true on Windows (above XP), false on Linux.
#else
        setChecked(dialog, "cursorCheckBox", true);
#endif
        setChecked(dialog, "saveAsCheckBox", true);
        setChecked(dialog, "previewGroupBox", true);
        dialog.findChild<QSpinBox *>("previewSizeSpinBox")->setValue(500);
        dialog.findChild<QComboBox *>("previewPositionComboBox")->setCurrentIndex(0);
        setChecked(dialog, "previewAutocloseCheckBox", true);
        dialog.findChild<QSpinBox *>("previewAutocloseTimeSpinBox")->setValue(30);
        dialog.findChild<QComboBox *>("previewAutocloseActionComboBox")->setCurrentIndex(2);
        dialog.findChild<QComboBox *>("previewDefaultActionComboBox")->setCurrentIndex(1);
        setChecked(dialog, "areaAutocloseCheckBox", true);
        setChecked(dialog, "historyCheckBox", false);

        setChecked(dialog, "closeHideCheckBox", false);
        setChecked(dialog, "clipboardCheckBox", false);
        setChecked(dialog, "urlClipboardCheckBox", true);
        setChecked(dialog, "optiPngCheckBox", true);
        setChecked(dialog, "currentMonitorCheckBox", true);
        dialog.findChild<QComboBox *>("hdrCaptureComboBox")->setCurrentIndex(1);
        setChecked(dialog, "replaceCheckBox", true);
        setChecked(dialog, "uploadCheckBox", true);

        const QList<QPair<const char *, const char *>> actions = {
            {"screen", "Shift+Print"}, {"area", "Ctrl+Shift+A"}, {"window", "Ctrl+Shift+W"},
            {"windowPicker", "Ctrl+Shift+P"}, {"open", "Ctrl+Shift+O"}, {"directory", "Ctrl+Shift+D"},
        };

        for (const auto &action : actions) {
            const QString name = action.first;
            QCheckBox *enabled = dialog.findChild<QCheckBox *>(name + "CheckBox");
            QVERIFY(enabled);
            enabled->setChecked(!enabled->isChecked());
            dialog.findChild<HotkeyWidget *>(name + "HotkeyWidget")->setHotkey(action.second);
        }

        dialog.findChild<QComboBox *>("uploadServiceComboBox")->setCurrentIndex(0); // The custom uploader, the only entry.
        dialog.findChild<QLineEdit *>("urlEdit")->setText("https://up.example.invalid/upload.php");
        dialog.findChild<QLineEdit *>("fieldEdit")->setText("image");
        dialog.findChild<QPlainTextEdit *>("headersEdit")->setPlainText("Authorization: Bearer token"); // One line: the golden file has one line per key.
        dialog.findChild<QLineEdit *>("linkPathEdit")->setText("data.link");
        dialog.findChild<QLineEdit *>("deletePathEdit")->setText("data.delete");

        dialog.saveSettings();
    }

    const QStringList saved = dump();

    if (qEnvironmentVariableIsSet("LIGHTSCREEN_PRINT_GOLDEN")) {
        qInfo().noquote() << "\n" + saved.join('\n');
    }

    COMPARE_LINES(saved, golden("roundtrip.golden"));

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        dialog.saveSettings();
    }

    // What the second save changes. On Linux, loadSettings forces the sound cue and cursor options off (both are
    // hidden there), and turns optimization off when neither oxipng nor optipng is on PATH (S05; until S05, when optipng
    // was not). Everything else survives.
    QStringList expected = saved;

#ifdef Q_OS_LINUX
    expected.replaceInStrings(QRegularExpression("^options/cursor=.*"), "options/cursor=false");
    expected.replaceInStrings(QRegularExpression("^options/playSound=.*"), "options/playSound=false");

    if (Optimizer::resolveTool(QString()).isEmpty()) {
        expected.replaceInStrings(QRegularExpression("^options/optimize=.*"), "options/optimize=false");
    }
#endif

    COMPARE_LINES(dump(), expected);
}

// Indexes the dialog's combo boxes cannot show (known-bugs.md, bug 19). The format combo box lists WEBP (3), so a
// WEBP setting is shown and saved as is; a naming or preview position index the boxes do not list is kept instead of
// being saved as -1. An "upload/service" the box does not list reads as 0 (M12).
void tst_Settings::dialogOutOfRangeIndexes()
{
    mSettings->setValue("file/format", 3);
    mSettings->setValue("file/naming", 9);
    mSettings->setValue("options/previewPosition", 9);
    mSettings->setValue("upload/service", 9);

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        QCOMPARE(dialog.findChild<QComboBox *>("formatComboBox")->currentText(), QStringLiteral("WEBP"));
        dialog.saveSettings();
    }

    QCOMPARE(mSettings->value("file/format").toString(), QStringLiteral("3"));
    QCOMPARE(mSettings->value("file/naming").toString(), QStringLiteral("9"));
    QCOMPARE(mSettings->value("options/previewPosition").toString(), QStringLiteral("9"));
    QCOMPARE(mSettings->value("upload/service").toString(), QStringLiteral("0"));
}

// The PNG optimizer choice (S05): "options/optimizer" stays absent until the user picks a tool, the combo box then
// shows the automatic choice; a picked tool is saved, and a stored one is shown and kept, found or not.
void tst_Settings::dialogOptimizerChoice()
{
    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        auto combo = dialog.findChild<QComboBox *>("optimizerComboBox");
        QVERIFY(combo);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->itemData(0).toString(), QStringLiteral("oxipng"));
        QCOMPARE(combo->itemData(1).toString(), QStringLiteral("optipng"));

        const QString automatic = Optimizer::resolveTool(QString());
        QCOMPARE(combo->currentData().toString(), automatic.isEmpty() ? QStringLiteral("oxipng") : automatic);

        for (int i = 0; i < combo->count(); ++i) {
            const bool found = !Optimizer::program(combo->itemData(i).toString()).isEmpty();
            QCOMPARE(combo->itemText(i).endsWith(" (not found)"), !found);
        }

        dialog.saveSettings();
    }

    QVERIFY(!mSettings->contains("options/optimizer"));

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        auto combo = dialog.findChild<QComboBox *>("optimizerComboBox");
        combo->setCurrentIndex(1);
        emit combo->activated(1);
        dialog.saveSettings();
    }

    QCOMPARE(mSettings->value("options/optimizer").toString(), QStringLiteral("optipng"));

    mSettings->setValue("options/optimizer", "oxipng");

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        QCOMPARE(dialog.findChild<QComboBox *>("optimizerComboBox")->currentData().toString(), QStringLiteral("oxipng"));
        dialog.saveSettings();
    }

    QCOMPARE(mSettings->value("options/optimizer").toString(), QStringLiteral("oxipng"));
}

// Settings written before M12 still load. "upload/service" 1 (pomf when Imgur was 0) is beyond the one-entry list and
// reads as 0, which is the custom uploader now; the old upload/imgur/* keys are left as they were. The pomf URL is
// migrated to the custom uploader's settings (S03) and also left as it was.
void tst_Settings::dialogLegacyImgurSettings()
{
    mSettings->setValue("upload/service", 1);
    mSettings->setValue("upload/pomf/pomf_url", "https://pomf.example.invalid");
    mSettings->setValue("upload/imgur/access_token", "token");
    mSettings->setValue("upload/imgur/account_username", "someone");
    mSettings->setValue("upload/imgur/album", "album");

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        QComboBox *service = dialog.findChild<QComboBox *>("uploadServiceComboBox");
        QCOMPARE(service->count(), 1);
        QCOMPARE(service->currentIndex(), 0);
        QCOMPARE(dialog.findChild<QLineEdit *>("urlEdit")->text(), QStringLiteral("https://pomf.example.invalid/upload.php"));
        QCOMPARE(dialog.findChild<QLineEdit *>("fieldEdit")->text(), QStringLiteral("files[]"));
        QCOMPARE(dialog.findChild<QLineEdit *>("linkPathEdit")->text(), QStringLiteral("files.0.url"));
        dialog.saveSettings();
    }

    QCOMPARE(mSettings->value("upload/service").toString(), QStringLiteral("0"));
    QCOMPARE(mSettings->value("upload/pomf/pomf_url").toString(), QStringLiteral("https://pomf.example.invalid"));
    QCOMPARE(mSettings->value("upload/custom/url").toString(), QStringLiteral("https://pomf.example.invalid/upload.php"));
    QCOMPARE(mSettings->value("upload/custom/field").toString(), QStringLiteral("files[]"));
    QCOMPARE(mSettings->value("upload/custom/linkPath").toString(), QStringLiteral("files.0.url"));
    QCOMPARE(mSettings->value("upload/imgur/access_token").toString(), QStringLiteral("token"));
    QCOMPARE(mSettings->value("upload/imgur/account_username").toString(), QStringLiteral("someone"));
    QCOMPARE(mSettings->value("upload/imgur/album").toString(), QStringLiteral("album"));
    QCOMPARE(Uploader::serviceName(mSettings->value("upload/service").toInt()), QStringLiteral("custom"));
}

// The custom uploader's headers are several lines in one key; they survive a save, the INI file and a reload.
void tst_Settings::dialogUploaderHeaders()
{
    const QString headers = QStringLiteral("Authorization: Bearer token\nX-Other: 1");

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        dialog.findChild<QPlainTextEdit *>("headersEdit")->setPlainText(headers);
        dialog.saveSettings();
    }

    mSettings->sync();
    QSettings reopened(mSettings->fileName(), QSettings::IniFormat);
    QCOMPARE(reopened.value("upload/custom/headers").toString(), headers);

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        QCOMPARE(dialog.findChild<QPlainTextEdit *>("headersEdit")->toPlainText(), headers);
    }
}

// A macro, not a function: QSKIP returns from the function it is used in.
#ifdef Q_OS_WIN
// applySettings would write the real HKCU Run key through os::setStartup.
#define REQUIRE_WINDOW_PLATFORM() \
    QSKIP("Constructs LightscreenWindow, whose applySettings writes the real Windows startup registry key.")
#else
#define REQUIRE_WINDOW_PLATFORM()                                                                   \
    if (QGuiApplication::platformName() != "xcb") {                                                 \
        QSKIP("Constructs LightscreenWindow, which needs an X display (run under xvfb-run).");      \
    }
#endif

// What screenshotAction reads when the settings hold only what the window needs to start quietly. "file/format"
// must exist or the constructor opens the options dialog instead. Since S06 the window's defaults are the dialog's
// (tools/settings): file saving on, prefix "screenshot.", the Screenshots folder under Documents (until S06: off,
// empty, the current directory). The cursor is on: only the dialog forces it off on Linux.
void tst_Settings::windowReadsEmptySettings()
{
    REQUIRE_WINDOW_PLATFORM();

    mSettings->setValue("file/format", 1);
    mSettings->setValue("options/tray", false);
    mSettings->setValue("options/disableUpdater", true);

    LightscreenWindow window;
    Screenshot::Options options;
    QVERIFY(probeWindow(window, &options));

    const QStringList expected = {
        "animations=true",
        "clipboard=true",
        "currentMonitor=false",
        "cursor=true",
        "directory=<documents>/Screenshots",
        "file=true",
        "format=1",
        "magnify=false",
        "naming.dateFormat=yyyy-MM-dd",
        "naming.flip=false",
        "naming.leadingZeros=0",
        "naming.naming=0",
        "optimize=false",
        "prefix=screenshot.",
        "preview=false",
        "quality=100",
        "replace=false",
        "saveAs=false",
        "upload=false",
        "uploadService=custom",
        "urlClipboard=false",
    };

    COMPARE_LINES(describe(options), expected);
}

// What screenshotAction reads from the dialog's defaults (tray and updater turned off so the window starts quietly).
void tst_Settings::windowReadsDialogDefaults()
{
    REQUIRE_WINDOW_PLATFORM();

    {
        OptionsDialog dialog;
        QTRY_VERIFY(dialog.isEnabled());
        dialog.saveSettings();
    }

    mSettings->setValue("options/tray", false);
    mSettings->setValue("options/disableUpdater", true);

    LightscreenWindow window;
    Screenshot::Options options;
    QVERIFY(probeWindow(window, &options));

    const QStringList expected = {
        "animations=true", // Not written by the dialog.
        "clipboard=true",
        "currentMonitor=false",
        "cursor=false", // The dialog forces it off on Linux.
        "directory=<documents>/Screenshots",
        "file=true",
        "format=0", // PNG since S06 (the dialog's default was JPEG, 1).
        "magnify=false",
        "naming.dateFormat=yyyy-MM-dd",
        "naming.flip=false",
        "naming.leadingZeros=0",
        "naming.naming=0",
        "optimize=false",
        "prefix=screenshot.",
        "preview=false",
        "quality=80", // Forced for PNG (bug 20).
        "replace=false",
        "saveAs=false",
        "upload=false",
        "uploadService=custom",
        "urlClipboard=false",
    };

    COMPARE_LINES(describe(options), expected);
}

// screenshotAction reads the settings once per window and caches them until applySettings runs (after the options
// dialog closes). A setting changed behind its back is not seen by the next screenshot.
void tst_Settings::windowCachesOptions()
{
    REQUIRE_WINDOW_PLATFORM();

    mSettings->setValue("file/format", 1);
    mSettings->setValue("file/prefix", "before");
    mSettings->setValue("options/tray", false);
    mSettings->setValue("options/disableUpdater", true);

    LightscreenWindow window;
    Screenshot::Options options;
    QVERIFY(probeWindow(window, &options));
    QCOMPARE(options.prefix, QStringLiteral("before"));

    mSettings->setValue("file/prefix", "after");
    QVERIFY(probeWindow(window, &options));
    QCOMPARE(options.prefix, QStringLiteral("before"));
}

// For PNG the Screenshot constructor replaces options/quality with 80 (known-bugs.md, bug 20). Other formats keep it.
void tst_Settings::windowPngQualityIsForced()
{
    REQUIRE_WINDOW_PLATFORM();

    mSettings->setValue("file/format", Screenshot::PNG);
    mSettings->setValue("options/quality", 30);
    mSettings->setValue("options/tray", false);
    mSettings->setValue("options/disableUpdater", true);

    {
        LightscreenWindow window;
        Screenshot::Options options;
        QVERIFY(probeWindow(window, &options));
        QCOMPARE(options.quality, 80);
    }

    mSettings->setValue("file/format", Screenshot::JPEG);

    LightscreenWindow window;
    Screenshot::Options options;
    QVERIFY(probeWindow(window, &options));
    QCOMPARE(options.quality, 30);
}

// Typing a folder path into the target box suggests its subfolders, and no "." or ".." (M04 replaced QDirModel).
void tst_Settings::targetFolderCompletes()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    QVERIFY(QDir(folder.path()).mkdir("alpha"));
    QVERIFY(QDir(folder.path()).mkdir("beta"));

    OptionsDialog dialog;
    QTRY_VERIFY(dialog.isEnabled());
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    QLineEdit *target = dialog.findChild<QLineEdit *>("targetLineEdit");
    QVERIFY(target);
    QVERIFY(target->completer());
    target->setFocus();
    target->clear();
    QTest::keyClicks(target, QDir::toNativeSeparators(folder.path()) + QDir::separator() + "a");

    QTRY_VERIFY_WITH_TIMEOUT(target->completer()->popup()->isVisible(), 5000);
    QCOMPARE(target->completer()->completionCount(), 1);
    QCOMPARE(target->completer()->currentCompletion(), QDir::toNativeSeparators(folder.path()) + QDir::separator() + "alpha");

    // Right after the separator: both subfolders and nothing else.
    target->clear();
    QTest::keyClicks(target, QDir::toNativeSeparators(folder.path()) + QDir::separator());
    QTRY_COMPARE(target->completer()->completionCount(), 2);
}

// The naming dialog's date format box takes no character a file name cannot hold, typed into its text field.
void tst_Settings::dateFormatRejectsFileNameCharacters()
{
    NamingDialog dialog(Screenshot::Date);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    QComboBox *format = dialog.findChild<QComboBox *>("dateFormatComboBox");
    QVERIFY(format && format->lineEdit());
    format->lineEdit()->setFocus();
    format->lineEdit()->clear();
#ifdef Q_OS_WIN
    QTest::keyClicks(format->lineEdit(), "yy?:*|MM/dd");
#else
    QTest::keyClicks(format->lineEdit(), "yy/MM/dd");
#endif
    QCOMPARE(format->currentText(), QString("yyMMdd"));
}

QTEST_MAIN(tst_Settings)
#include "tst_settings.moc"
