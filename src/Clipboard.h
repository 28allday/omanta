#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration>

// File cut/copy/paste, on the real system clipboard.
//
// Uses `x-special/gnome-copied-files`, the format every GTK file manager reads
// and writes, alongside `text/uri-list`. That is what makes copying here and
// pasting in Nautilus — or a file chooser, or a chat window — actually work.
// A private in-process clipboard would look identical in testing and be useless
// in practice.
class Clipboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool hasFiles READ hasFiles NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit Clipboard(QObject *parent = nullptr);

    bool hasFiles() const { return !paths().isEmpty(); }
    int count() const { return int(paths().size()); }

    Q_INVOKABLE void copyFiles(const QStringList &paths);
    Q_INVOKABLE void cutFiles(const QStringList &paths);

    // Read back whatever is on the clipboard now — which may have been put
    // there by another application.
    Q_INVOKABLE QStringList paths() const;
    Q_INVOKABLE bool isCut() const;
    Q_INVOKABLE void clear();
    // Plain text (Copy Location) — nothing to do with the file payload above.
    Q_INVOKABLE void copyText(const QString &text);

Q_SIGNALS:
    void changed();

private:
    void put(const QStringList &paths, bool cut);
};
