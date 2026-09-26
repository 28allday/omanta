#include "DefaultFileManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {

QString configHome()
{
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    return xdg.isEmpty() ? QDir::homePath() + QStringLiteral("/.config") : xdg;
}

QString findSwitcher()
{
    // OMANTA_SWITCH points the tests at the source tree's copy.
    const QString forced = qEnvironmentVariable("OMANTA_SWITCH");
    if (!forced.isEmpty())
        return QFileInfo(forced).isExecutable() ? forced : QString();

    // Installed beside the binary by the package and by bin/install; PATH
    // covers anything else.
    const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/omanta-switch");
    if (QFileInfo(sibling).isExecutable())
        return sibling;
    return QStandardPaths::findExecutable(QStringLiteral("omanta-switch"));
}

} // namespace

DefaultFileManager::DefaultFileManager(QObject *parent)
    : QObject(parent)
    , m_program(findSwitcher())
{
    refresh();
}

bool DefaultFileManager::omarchy() const
{
    return QFileInfo(configHome() + QStringLiteral("/omarchy")).isDir();
}

void DefaultFileManager::refresh()
{
    run({ QStringLiteral("status") });
}

void DefaultFileManager::setDefault(bool omanta)
{
    run({ omanta ? QStringLiteral("omanta") : QStringLiteral("nautilus") });
    refresh();
}

void DefaultFileManager::setMenuInstalled(bool installed)
{
    run({ installed ? QStringLiteral("install-menu") : QStringLiteral("remove-menu") });
    refresh();
}

void DefaultFileManager::offerToggleMenu(Settings *settings)
{
    if (!settings || settings->toggleMenuOffered() || !available() || !omarchy())
        return;
    // Recorded before the script runs, so a second window opening meanwhile
    // cannot offer it again.
    settings->setToggleMenuOffered(true);
    // install-menu replaces its own managed block, so a row already there
    // (from running the command by hand) is left exactly as it was.
    setMenuInstalled(true);
}

void DefaultFileManager::run(const QStringList &arguments)
{
    if (!available())
        return;
    m_queue.append(arguments);
    if (!m_process)
        startNext();
}

void DefaultFileManager::startNext()
{
    if (m_queue.isEmpty()) {
        setBusy(false);
        return;
    }

    const QStringList arguments = m_queue.takeFirst();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::finished, this, [this, arguments](int code, QProcess::ExitStatus status) {
        QProcess *process = m_process;
        m_process = nullptr;
        const QString output = QString::fromUtf8(process->readAllStandardOutput());
        const QString errors = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();

        if (status != QProcess::NormalExit || code != 0) {
            m_lastError = errors.isEmpty() ? tr("omanta-switch %1 failed").arg(arguments.join(QLatin1Char(' ')))
                                           : errors;
            Q_EMIT statusChanged();
        } else if (arguments.first() == QLatin1String("status")) {
            parseStatus(output);
        } else {
            m_lastError.clear();
        }

        startNext();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // A process that never started never finishes; unstick the queue.
        if (error != QProcess::FailedToStart || !m_process)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        m_queue.clear();
        m_lastError = tr("Could not run omanta-switch");
        Q_EMIT statusChanged();
        setBusy(false);
    });

    setBusy(true);
    m_process->start(m_program, arguments);
}

void DefaultFileManager::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void DefaultFileManager::parseStatus(const QString &output)
{
    // "key : value" lines; see omanta-switch's status().
    bool isDefault = false;
    bool menu = false;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("inode/directory default"))
            isDefault = value == QLatin1String("omanta.desktop");
        else if (key == QLatin1String("Toggle-menu entry"))
            menu = value == QLatin1String("installed");
    }
    m_isDefault = isDefault;
    m_menuInstalled = menu;
    m_known = true;
    Q_EMIT statusChanged();
}
