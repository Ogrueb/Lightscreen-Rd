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
#include <tools/settings.h>
#include <tools/os.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QHash>
#include <QSettings>

namespace {

// The defaults that do not depend on anything. Where the options dialog and the main window used to disagree, the
// dialog's value won, except file/format: PNG (0), which the window already used, not the dialog's JPEG (1).
const QHash<QString, QVariant> &fixedDefaults()
{
    static const QHash<QString, QVariant> defaults = {
        { "file/enabled", true },
        { "file/format", 0 },
        { "file/naming", 0 },

        { "options/animations", true },
        { "options/areaAutoclose", false },
        { "options/clipboard", true },
        { "options/closeHide", true },
        { "options/closeToTrayWarning", true },
        { "options/currentMonitor", false },
        { "options/cursor", true },
        { "options/delay", 0 },
        { "options/disableHideAlert", false },
        { "options/disableUpdater", false },
        { "options/flip", false },
        { "options/hdrCapture", 0 }, // 0 automatic, 1 off (bug 9); set to 1 after a crash in the HDR path.
        { "options/hdrCheckAgainstGdi", false }, // Settings file only: compare each HDR screen capture with Qt's grab.
        { "options/hide", true },
        { "options/magnify", false },
        { "options/message", true },
        { "options/naming/dateFormat", QStringLiteral("yyyy-MM-dd") },
        { "options/naming/leadingZeros", 0 },
        { "options/optimize", false },
        { "options/playSound", false },
        { "options/preview", false },
        { "options/previewAutoclose", false },
        { "options/previewAutocloseAction", 0 },
        { "options/previewAutocloseTime", 15 },
        { "options/previewDefaultAction", 0 },
        { "options/previewPosition", 3 },
        { "options/previewSize", 300 },
        { "options/quality", 100 },
        { "options/replace", false },
        { "options/saveAs", false },
        { "options/startup", false },
        { "options/startupHide", true },
        { "options/tray", true },
        { "options/uploadAuto", false },
        { "options/urlClipboard", false },

        { "actions/screen/enabled", true },
        { "actions/screen/hotkey", QStringLiteral("Print") },
        { "actions/area/enabled", false },
        { "actions/area/hotkey", QStringLiteral("Ctrl+Print") },
        { "actions/window/enabled", false },
        { "actions/window/hotkey", QStringLiteral("Alt+Print") },
        { "actions/windowPicker/enabled", false },
        { "actions/windowPicker/hotkey", QStringLiteral("Ctrl+Alt+Print") },
        { "actions/open/enabled", false },
        { "actions/open/hotkey", QStringLiteral("Ctrl+PgUp") },
        { "actions/directory/enabled", false },
        { "actions/directory/hotkey", QStringLiteral("Ctrl+PgDown") },

        { "upload/service", 0 },
    };

    return defaults;
}

} // namespace

namespace Settings {

QVariant defaultValue(const QString &key, bool portable)
{
    // The texts keep the options dialog's translation context, where they were before.
    if (key == QLatin1String("file/prefix")) {
        return QCoreApplication::translate("OptionsDialog", "screenshot.");
    }

    if (key == QLatin1String("file/target")) {
        const QString folder = QCoreApplication::translate("OptionsDialog", "Screenshots");
        return portable ? folder : os::getDocumentsPath() + QDir::separator() + folder;
    }

    if (key == QLatin1String("options/history")) {
        return !portable;
    }

    return fixedDefaults().value(key);
}

QVariant value(const QSettings *settings, const QString &key, bool portable)
{
    const QString group = settings->group();
    return settings->value(key, defaultValue(group.isEmpty() ? key : group + '/' + key, portable));
}

void applyApplicationIdentity()
{
    QCoreApplication::setOrganizationName(QStringLiteral("K"));
    QCoreApplication::setApplicationName(QStringLiteral("Lightscreen"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Lightscreen Rd"));
}

QStringList keys()
{
    QStringList keys = fixedDefaults().keys();
    keys << "file/prefix" << "file/target" << "options/history";
    keys.sort();
    return keys;
}

} // namespace Settings
