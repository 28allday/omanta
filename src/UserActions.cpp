#include "UserActions.h"
#include "Location.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantMap>

#include <gio/gio.h>

namespace {

// The same minimal TOML the theme reader speaks: `key = value` lines, where a
// value is a quoted string, a bool, or a one-line array of quoted strings.
// Nothing nested, nothing multi-line — and no third-party parser for it.
QString unquote(const QString &raw)
{
    QString value = raw.trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        return value.mid(1, value.size() - 2);
    return value;
}

QStringList parseArray(const QString &raw)
{
    QString inner = raw.trimmed();
    if (!inner.startsWith(QLatin1Char('[')) || !inner.endsWith(QLatin1Char(']')))
        return {};
    inner = inner.mid(1, inner.size() - 2);
    QStringList values;
    for (const QString &part : inner.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString value = unquote(part);
        if (!value.isEmpty())
            values.append(value);
    }
    return values;
}

// POSIX single-quote escaping, for the one place a command becomes a shell
// string: the floating-terminal wrapper's `bash -c` argument.
QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString contentTypeFor(const QString &path, bool isDir)
{
    if (isDir)
        return QStringLiteral("inode/directory");
    // Name-based: eligibility is a menu decision made per right-click, and
    // sniffing every selected file's bytes on each menu open is not on.
    gboolean uncertain = FALSE;
    char *type = g_content_type_guess(QFileInfo(path).fileName().toUtf8().constData(),
                                      nullptr, 0, &uncertain);
    const QString result = QString::fromUtf8(type ? type : "");
    g_free(type);
    return result;
}

bool matchesMime(const QString &type, const QStringList &patterns)
{
    for (const QString &pattern : patterns) {
        if (pattern == QLatin1String("*"))
            return true;
        if (pattern.endsWith(QLatin1String("/*"))) {
            if (type.startsWith(QStringView(pattern).left(pattern.size() - 1)))
                return true;
        } else if (type == pattern) {
            return true;
        }
    }
    return false;
}

} // namespace

UserActions::UserActions(QObject *parent)
    : QObject(parent)
{
    m_watcher = new QFileSystemWatcher(this);
    for (const QString &dir : actionDirs()) {
        if (QDir(dir).exists())
            m_watcher->addPath(dir);
    }
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &UserActions::reload);
    reload();
}

QStringList UserActions::actionDirs() const
{
    // Test override first; otherwise user dir shadows the shipped dir.
    const QString override = qEnvironmentVariable("OMANTA_ACTIONS_DIR");
    if (!override.isEmpty())
        return { override };
    return { QStringLiteral("/usr/share/omanta/actions"),
             QDir::homePath() + QStringLiteral("/.config/omanta/actions") };
}

void UserActions::reload()
{
    // Later dirs override earlier ones by file basename — user beats shipped.
    QMap<QString, Action> byId;

    for (const QString &dirPath : actionDirs()) {
        const QDir dir(dirPath);
        const QStringList files = dir.entryList({ QStringLiteral("*.toml") },
                                                QDir::Files, QDir::Name);
        for (const QString &fileName : files) {
            QFile file(dir.filePath(fileName));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            Action action;
            action.id = QFileInfo(fileName).completeBaseName();

            const QStringList lines = QString::fromUtf8(file.readAll())
                                          .split(QLatin1Char('\n'));
            for (const QString &raw : lines) {
                const QString line = raw.trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;
                const qsizetype eq = line.indexOf(QLatin1Char('='));
                if (eq < 0)
                    continue;
                const QString key = line.left(eq).trimmed();
                const QString value = line.mid(eq + 1).trimmed();

                if (key == QLatin1String("name"))
                    action.name = unquote(value);
                else if (key == QLatin1String("plural"))
                    action.plural = unquote(value);
                else if (key == QLatin1String("command"))
                    action.command = unquote(value);
                else if (key == QLatin1String("requires"))
                    action.requires_ = parseArray(value);
                else if (key == QLatin1String("mimetypes"))
                    action.mimetypes = parseArray(value);
                else if (key == QLatin1String("extensions"))
                    action.extensions = parseArray(value);
                else if (key == QLatin1String("each"))
                    action.each = value == QLatin1String("true");
                else if (key == QLatin1String("terminal"))
                    action.terminal = value == QLatin1String("true");
                else if (key == QLatin1String("directories"))
                    action.directories = value != QLatin1String("false");
                else if (key == QLatin1String("under"))
                    action.under = parseArray(value);
            }

            if (!action.name.isEmpty() && !action.command.isEmpty())
                byId.insert(action.id, action);
        }
    }

    m_actions = byId.values();
    Q_EMIT actionsChanged();
}

QString UserActions::resolveExecutable(const QString &name)
{
    // PATH first, then where things actually install on Omarchy — the
    // omarchy-send extension's hard-won lesson: the graphical session's PATH
    // often lacks ~/.local/bin.
    const QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty())
        return found;

    QStringList fallbackDirs;
    const QString binDir = qEnvironmentVariable("BIN_DIR");
    if (!binDir.isEmpty())
        fallbackDirs.append(binDir);
    fallbackDirs.append(QDir::homePath() + QStringLiteral("/.local/bin"));
    fallbackDirs.append(QDir::homePath() + QStringLiteral("/bin"));
    fallbackDirs.append(QDir::homePath()
                        + QStringLiteral("/.local/share/omarchy/bin"));
    fallbackDirs.append(QStringLiteral("/usr/share/omarchy/bin"));

    for (const QString &dir : std::as_const(fallbackDirs)) {
        const QFileInfo candidate(QDir(dir).filePath(name));
        if (candidate.isFile() && candidate.isExecutable())
            return candidate.absoluteFilePath();
    }
    return {};
}

bool UserActions::available(const Action &action) const
{
    for (const QString &required : action.requires_) {
        if (resolveExecutable(required).isEmpty())
            return false;
    }
    if (action.terminal
        && resolveExecutable(
               QStringLiteral("omarchy-launch-floating-terminal-with-presentation"))
               .isEmpty())
        return false;
    return true;
}

QStringList UserActions::eligiblePaths(const Action &action, const QStringList &paths) const
{
    QStringList eligible;
    for (const QString &path : paths) {
        if (!Location::isLocal(path))
            continue;
        if (!action.under.isEmpty()) {
            bool within = false;
            for (const QString &rootSpec : action.under) {
                QString root = rootSpec;
                if (root == QLatin1String("~"))
                    root = QDir::homePath();
                else if (root.startsWith(QLatin1String("~/")))
                    root = QDir::homePath() + root.mid(1);
                if (path == root || path.startsWith(root + QLatin1Char('/'))) {
                    within = true;
                    break;
                }
            }
            if (!within)
                continue;
        }
        const bool isDir = QFileInfo(path).isDir();
        if (isDir && !action.directories)
            continue;

        bool matches = action.mimetypes.isEmpty()
                       || matchesMime(contentTypeFor(path, isDir), action.mimetypes);
        if (!matches) {
            const QString lower = path.toLower();
            for (const QString &extension : action.extensions) {
                if (lower.endsWith(extension.toLower())) {
                    matches = true;
                    break;
                }
            }
        }
        if (matches && !eligible.contains(path))
            eligible.append(path);
    }
    return eligible;
}

QVariantList UserActions::actionsFor(const QStringList &paths) const
{
    QVariantList result;
    if (paths.isEmpty())
        return result;

    for (const Action &action : m_actions) {
        if (!available(action))
            continue;
        const QStringList eligible = eligiblePaths(action, paths);
        if (eligible.isEmpty())
            continue;

        QString label = action.name;
        if (eligible.size() > 1 && !action.plural.isEmpty()) {
            label = action.plural;
            label.replace(QLatin1String("%n"), QString::number(eligible.size()));
        }
        result.append(QVariantMap{ { QStringLiteral("id"), action.id },
                                   { QStringLiteral("label"), label } });
    }
    return result;
}

QList<QStringList> UserActions::invocationsFor(const QString &id,
                                               const QStringList &paths) const
{
    for (const Action &action : m_actions) {
        if (action.id != id)
            continue;
        if (!available(action))
            return {};
        const QStringList eligible = eligiblePaths(action, paths);
        if (eligible.isEmpty())
            return {};

        // The command is split on spaces — action commands are flags and
        // placeholders, not arbitrary shell. Anything with spaces arrives
        // via %f/%F, which are substituted after the split, whole.
        const QStringList tokens =
            action.command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            return {};

        // One argv per process run: %F actions make one, `each` actions one
        // per path.
        QList<QStringList> argvs;
        const QList<QStringList> pathSets = action.each
            ? [&] { QList<QStringList> sets;
                    for (const QString &path : eligible)
                        sets.append(QStringList{ path });
                    return sets; }()
            : QList<QStringList>{ eligible };

        for (const QStringList &pathSet : pathSets) {
            QStringList argv;
            for (const QString &token : tokens) {
                if (token == QLatin1String("%F") || token == QLatin1String("%f"))
                    argv.append(pathSet);
                else
                    argv.append(token);
            }
            const QString program = resolveExecutable(argv.constFirst());
            if (program.isEmpty())
                return {};
            argv[0] = program;
            argvs.append(argv);
        }

        if (!action.terminal)
            return argvs;

        // Terminal actions become one floating presentation terminal running
        // the commands in sequence; a failure moves on to the next file
        // rather than aborting the batch.
        QStringList shellParts;
        for (const QStringList &argv : std::as_const(argvs)) {
            QStringList quoted;
            for (const QString &part : argv)
                quoted.append(shellQuote(part));
            shellParts.append(quoted.join(QLatin1Char(' '))
                              + QStringLiteral(" || true"));
        }
        const QString wrapper = resolveExecutable(
            QStringLiteral("omarchy-launch-floating-terminal-with-presentation"));
        return { QStringList{ wrapper, shellParts.join(QStringLiteral("; ")) } };
    }
    return {};
}

void UserActions::run(const QString &id, const QStringList &paths)
{
    const QList<QStringList> invocations = invocationsFor(id, paths);
    for (const QStringList &argv : invocations) {
        if (argv.size() < 1)
            continue;
        QProcess::startDetached(argv.constFirst(), argv.mid(1));
    }
}
