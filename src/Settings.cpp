#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    load();

    // Watch for another process (or a hand edit) changing the file — same
    // re-arm dance as the starred and servers stores.
    m_watcher = new QFileSystemWatcher(this);
    if (QFile::exists(filePath()))
        m_watcher->addPath(filePath());
    const QString dir = QFileInfo(filePath()).absolutePath();
    if (QDir(dir).exists())
        m_watcher->addPath(dir);
    auto rearm = [this] {
        if (m_saving)
            return; // our own write; state is already current
        load();
        if (QFile::exists(filePath()) && !m_watcher->files().contains(filePath()))
            m_watcher->addPath(filePath());
        Q_EMIT changed();
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, rearm);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, rearm);
}

QString Settings::filePath() const
{
    const QString override = qEnvironmentVariable("OMANTA_SETTINGS_FILE");
    if (!override.isEmpty())
        return override;
    return QDir::homePath() + QStringLiteral("/.config/omanta/settings");
}

void Settings::load()
{
    m_values.clear();

    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || eq <= 0)
            continue;
        m_values.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
}

void Settings::save()
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QFile file(filePath());
    m_saving = true;
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QStringList keys = m_values.keys();
        keys.sort(); // stable file, readable diffs
        for (const QString &key : std::as_const(keys))
            file.write(key.toUtf8() + '=' + m_values.value(key).toUtf8() + '\n');
        file.close();
    }
    if (!m_watcher->files().contains(filePath()))
        m_watcher->addPath(filePath());
    // The watcher delivers our own write asynchronously; lift the guard once
    // that delivery has had its turn on the loop.
    QTimer::singleShot(200, this, [this] { m_saving = false; });
}

void Settings::set(const QString &key, const QString &value)
{
    if (m_values.value(key) == value)
        return;
    m_values.insert(key, value);
    save();
    Q_EMIT changed();
}

bool Settings::boolFor(const char *key, bool fallback) const
{
    const QString value = m_values.value(QLatin1String(key));
    if (value == QLatin1String("true"))
        return true;
    if (value == QLatin1String("false"))
        return false;
    return fallback;
}

QString Settings::choiceFor(const char *key, const QStringList &allowed) const
{
    const QString value = m_values.value(QLatin1String(key));
    if (allowed.contains(value))
        return value;
    return allowed.first();
}

qreal Settings::realFor(const char *key, qreal fallback, qreal min, qreal max) const
{
    bool ok = false;
    const qreal value = m_values.value(QLatin1String(key)).toDouble(&ok);
    if (!ok || value < min || value > max)
        return fallback;
    return value;
}

QStringList Settings::allListColumns()
{
    // Canonical order, Nautilus's default-column-order pared to the columns
    // that exist here (where/recency/starred belong to search and Recent
    // views, which have no configurable columns yet).
    return { QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("type"),
             QStringLiteral("owner"), QStringLiteral("group"), QStringLiteral("permissions"),
             QStringLiteral("modified"), QStringLiteral("created"), QStringLiteral("accessed") };
}

QStringList Settings::columnListFor(const char *key, const QStringList &fallback) const
{
    if (!m_values.contains(QLatin1String(key)))
        return fallback;
    const QStringList known = allListColumns();
    QStringList out;
    const QStringList parts = m_values.value(QLatin1String(key)).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        const QString id = raw.trimmed();
        if (known.contains(id) && !out.contains(id))
            out.append(id);
    }
    return out.isEmpty() ? fallback : out;
}

QStringList Settings::allCaptionFields()
{
    QStringList fields = allListColumns();
    fields.removeAll(QStringLiteral("name"));
    return fields;
}

QStringList Settings::iconCaptions() const
{
    // Always exactly three slots: an unknown id becomes "none" in place (so
    // a typo in slot one cannot promote slot two), short lists pad, long
    // lists truncate.
    const QStringList fields = allCaptionFields();
    QStringList out;
    const QStringList parts = m_values.value(QStringLiteral("iconCaptions"))
                                  .split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        const QString id = raw.trimmed();
        out.append(fields.contains(id) ? id : QStringLiteral("none"));
    }
    while (out.size() < 3)
        out.append(QStringLiteral("none"));
    return out.mid(0, 3);
}

QStringList Settings::listColumnOrder() const
{
    // A valid order is a permutation of every id with name first — repair
    // whatever the file holds rather than leaking a partial list into the
    // dialog: drop unknowns, then append anything missing in canonical order.
    QStringList order = columnListFor("listColumnOrder", allListColumns());
    for (const QString &id : allListColumns()) {
        if (!order.contains(id))
            order.append(id);
    }
    order.removeAll(QStringLiteral("name"));
    order.prepend(QStringLiteral("name"));
    return order;
}

QStringList Settings::listVisibleColumns() const
{
    // GNOME's schema default: name, size, date_modified.
    QStringList visible = columnListFor("listVisibleColumns",
        { QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("modified") });
    if (!visible.contains(QStringLiteral("name")))
        visible.prepend(QStringLiteral("name"));

    // Present them in the user's column order, name always first.
    QStringList out;
    for (const QString &id : listColumnOrder()) {
        if (visible.contains(id))
            out.append(id);
    }
    return out;
}
