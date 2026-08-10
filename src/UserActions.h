#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration>

class QFileSystemWatcher;

// User-defined context-menu actions — the replacement for the three
// nautilus-python extensions (transcode, omarchy-send, localsend).
//
// An action is one TOML file in /usr/share/omanta/actions (shipped) or
// ~/.config/omanta/actions (user; a user file with the same name overrides
// the shipped one). The whole surface:
//
//   name = "Transcode"                    # menu label (required)
//   plural = "Transcode %n items"         # label when several are eligible
//   command = "omarchy-transcode %f"      # required. %F = all paths, %f = per-file
//   each = true                           # run the command once per path (%f)
//   terminal = true                       # run in the floating presentation terminal
//   requires = ["omarchy-transcode"]      # hidden unless every executable resolves
//   mimetypes = ["image/*", "video/*"]    # eligible types; default any
//   extensions = [".heic"]                # eligible when the mimetype misses
//   directories = false                   # folders eligible? default true
//   under = ["~/Dropbox"]                 # eligible only inside these folders
//
// Actions see local paths only — a trash:// or smb:// selection is not
// something `omarchy-send` can take on argv.
class UserActions : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit UserActions(QObject *parent = nullptr);

    // The actions applicable to this selection: [{id, label}], in file-name
    // order. An action appears when at least one selected item is eligible.
    Q_INVOKABLE QVariantList actionsFor(const QStringList &paths) const;

    // Run action `id` on the eligible subset of `paths`, detached.
    Q_INVOKABLE void run(const QString &id, const QStringList &paths);

    // The exact invocation run() would make: argv for a plain action, or
    // [wrapper, shell-string] for a terminal one; one entry per process.
    // Public so the tests assert on commands instead of side effects.
    QList<QStringList> invocationsFor(const QString &id, const QStringList &paths) const;

    Q_INVOKABLE void reload();

Q_SIGNALS:
    void actionsChanged();

private:
    struct Action {
        QString id;      // file basename without .toml
        QString name;
        QString plural;
        QString command;
        QStringList requires_; // trailing _: "requires" is a MSVC-ism, stay clear
        QStringList mimetypes;
        QStringList extensions;
        QStringList under; // eligible only for paths inside these roots
        bool each = false;
        bool terminal = false;
        bool directories = true;
    };

    QStringList actionDirs() const;
    bool available(const Action &action) const;
    QStringList eligiblePaths(const Action &action, const QStringList &paths) const;
    static QString resolveExecutable(const QString &name);

    QList<Action> m_actions;
    QFileSystemWatcher *m_watcher = nullptr;
};
