#pragma once

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <utime.h>

// A throwaway directory tree the tests can mutate freely.
//
// Every test gets its own, because these tests exercise a live directory
// monitor — sharing a directory between tests would let one test's inotify
// events land in another's model.
class TempTree
{
public:
    // /tmp is a tmpfs on most systems and the freedesktop trash spec defines no
    // trash directory for one, so g_file_trash() fails there. Tests that touch
    // trash must run on a filesystem that has one — in practice, under $HOME.
    enum Location { SystemTemp, UnderHome };

    explicit TempTree(Location location = SystemTemp)
        : m_dir(location == UnderHome
                    ? QDir(QDir::homePath()).filePath(QStringLiteral(".omanta-test-XXXXXX"))
                    : QDir::tempPath() + QStringLiteral("/omanta-test-XXXXXX"))
    {
        Q_ASSERT(m_dir.isValid());
    }

    QString path() const { return m_dir.path(); }
    QString filePath(const QString &name) const { return QDir(m_dir.path()).filePath(name); }

    // Writes a file of exactly `bytes` length so size-ordering is testable.
    QString writeFile(const QString &name, int bytes = 8) const
    {
        const QString target = filePath(name);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(QByteArray(bytes, 'x'));
        file.close();
        return target;
    }

    QString makeDir(const QString &name) const
    {
        const QString target = filePath(name);
        QDir().mkpath(target);
        return target;
    }

    void remove(const QString &name) const { QFile::remove(filePath(name)); }

    void rename(const QString &from, const QString &to) const
    {
        QFile::rename(filePath(from), filePath(to));
    }

    // Deterministic timestamps — "now" is useless for asserting sort order.
    void setModified(const QString &name, const QDateTime &when) const
    {
        utimbuf times;
        times.actime = when.toSecsSinceEpoch();
        times.modtime = when.toSecsSinceEpoch();
        utime(filePath(name).toUtf8().constData(), &times);
    }

private:
    QTemporaryDir m_dir;
};
