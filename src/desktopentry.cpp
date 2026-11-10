#include "desktopentry.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QUrl>

namespace {
QString localizedName(const QHash<QString, QString> &values)
{
    QStringList keys;
    const QLocale locale = QLocale::system();
    const QString full = locale.name();
    if (!full.isEmpty()) {
        keys.append(QStringLiteral("Name[") + full + QLatin1Char(']'));
        const int underscore = full.indexOf(QLatin1Char('_'));
        if (underscore > 0)
            keys.append(QStringLiteral("Name[") + full.left(underscore) + QLatin1Char(']'));
    }
    if (!full.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
        keys.append(QStringLiteral("Name[zh_CN]"));
        keys.append(QStringLiteral("Name[zh]"));
    }
    keys.append(QStringLiteral("Name"));
    for (const QString &key : keys) {
        const QString value = values.value(key).trimmed();
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

QStringList splitExecCommand(const QString &command)
{
    QStringList result;
    QString current;
    bool singleQuote = false;
    bool doubleQuote = false;
    bool escaped = false;
    for (int i = 0; i < command.size(); ++i) {
        const QChar ch = command.at(i);
        if (escaped) {
            current.append(ch);
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\') && !singleQuote) {
            escaped = true;
            continue;
        }
        if (ch == QLatin1Char('"') && !singleQuote) {
            doubleQuote = !doubleQuote;
            continue;
        }
        if (ch == QLatin1Char('\'') && !doubleQuote) {
            singleQuote = !singleQuote;
            continue;
        }
        if (ch.isSpace() && !singleQuote && !doubleQuote) {
            if (!current.isEmpty()) {
                result.append(current);
                current.clear();
            }
            continue;
        }
        current.append(ch);
    }
    if (!current.isEmpty())
        result.append(current);
    return result;
}
}

DesktopEntry readDesktopEntry(const QString &desktopFilePath)
{
    DesktopEntry result;
    QFile file(desktopFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;
    if (file.size() > 2 * 1024 * 1024) {
        file.close();
        return result;
    }
    QHash<QString, QString> values;
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    bool inDesktopEntry = false;
    bool foundGroup = false;
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            const QString group = trimmed.mid(1, trimmed.size() - 2);
            inDesktopEntry = (group == QStringLiteral("Desktop Entry"));
            if (inDesktopEntry)
                foundGroup = true;
            continue;
        }
        if (!inDesktopEntry)
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        // Unescape the most common desktop-entry string escapes.
        value.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
        value.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
        value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        values.insert(key, value);
    }
    file.close();
    if (!foundGroup)
        return result;

    result.valid = true;
    result.type = values.value(QStringLiteral("Type")).trimmed();
    result.name = localizedName(values);
    if (result.name.isEmpty()) {
        result.name = QFileInfo(desktopFilePath).completeBaseName();
    }
    result.icon = values.value(QStringLiteral("Icon")).trimmed();
    result.exec = values.value(QStringLiteral("Exec")).trimmed();
    const QStringList mimeValues = values.value(QStringLiteral("MimeType")).split(
        QLatin1Char(';'), QString::SkipEmptyParts);
    for (const QString &mime : mimeValues) {
        const QString trimmedMime = mime.trimmed();
        if (!trimmedMime.isEmpty())
            result.mimeTypes.append(trimmedMime);
    }
    const QString terminal = values.value(QStringLiteral("Terminal")).trimmed().toLower();
    result.terminal = (terminal == QStringLiteral("true"));
    const QString noDisplay = values.value(QStringLiteral("NoDisplay")).trimmed().toLower();
    result.noDisplay = (noDisplay == QStringLiteral("true"));
    const QString hidden = values.value(QStringLiteral("Hidden")).trimmed().toLower();
    result.hidden = (hidden == QStringLiteral("true"));
    return result;
}

bool launchDesktopEntry(const QString &desktopFilePath, QString *error)
{
    if (!QFileInfo::exists(desktopFilePath)) {
        if (error) *error = QStringLiteral("启动器文件不存在");
        return false;
    }
    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (!gio.isEmpty()) {
        QProcess process;
        process.start(gio, QStringList() << QStringLiteral("launch") << desktopFilePath);
        if (process.waitForStarted(2500)) {
            if (!process.waitForFinished(6000) || process.exitStatus() != QProcess::NormalExit) {
                // The launch request was accepted; do not start a second copy.
                return true;
            }
            if (process.exitCode() == 0)
                return true;
        }
    }

    const DesktopEntry entry = readDesktopEntry(desktopFilePath);
    QString command = entry.exec;
    command.remove(QRegularExpression(QStringLiteral("%[fFuUdDnNickvm]")));
    QStringList arguments = splitExecCommand(command);
    if (arguments.isEmpty()) {
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(desktopFilePath)))
            return true;
        if (error) *error = QStringLiteral("没有可用的启动命令");
        return false;
    }
    QString program = arguments.takeFirst();
    if (entry.terminal) {
        const QString terminal = QStandardPaths::findExecutable(QStringLiteral("x-terminal-emulator"));
        if (!terminal.isEmpty()) {
            arguments.prepend(program);
            arguments.prepend(QStringLiteral("-e"));
            program = terminal;
        }
    }
    if (QProcess::startDetached(program, arguments))
        return true;
    if (error) *error = QStringLiteral("无法启动应用程序");
    return false;
}

bool launchDesktopEntryWithFile(const QString &desktopFilePath, const QString &filePath,
                                QString *error)
{
    if (!QFileInfo::exists(desktopFilePath) || !QFileInfo::exists(filePath)) {
        if (error) *error = QStringLiteral("应用或文件不存在");
        return false;
    }
    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (!gio.isEmpty()) {
        QProcess process;
        process.start(gio, QStringList() << QStringLiteral("launch")
                                         << desktopFilePath << filePath);
        if (process.waitForStarted(2500)) {
            if (process.waitForFinished(6000) && process.exitStatus() == QProcess::NormalExit
                && process.exitCode() == 0) {
                return true;
            }
        }
    }

    const DesktopEntry entry = readDesktopEntry(desktopFilePath);
    QString command = entry.exec;
    const bool hadFileCode = command.contains(QRegularExpression(
        QStringLiteral("%[fFuU]")));
    QString escapedFile = filePath;
    escapedFile.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escapedFile = QLatin1Char('"') + escapedFile + QLatin1Char('"');
    command.replace(QRegularExpression(QStringLiteral("%[fFuU]")), escapedFile);
    command.remove(QRegularExpression(QStringLiteral("%[dDnNickvm]")));
    if (!hadFileCode)
        command += QLatin1Char(' ') + escapedFile;
    QStringList arguments = splitExecCommand(command);
    if (arguments.isEmpty()) {
        if (error) *error = QStringLiteral("没有可用的启动命令");
        return false;
    }
    QString program = arguments.takeFirst();
    if (entry.terminal) {
        const QString terminal = QStandardPaths::findExecutable(QStringLiteral("x-terminal-emulator"));
        if (!terminal.isEmpty()) {
            arguments.prepend(program);
            arguments.prepend(QStringLiteral("-e"));
            program = terminal;
        }
    }
    if (QProcess::startDetached(program, arguments))
        return true;
    if (error) *error = QStringLiteral("无法启动应用程序");
    return false;
}
