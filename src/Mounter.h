#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>

#include <gio/gio.h>

// Mounting, and the questions it asks. One instance per window drives both
// ways a mount starts — navigating to an unmounted location (smb:// typed
// into the path bar, a bookmark to a share) and clicking an unmounted volume
// in the sidebar (PlacesModel borrows operations from here).
//
// GMountOperation's ask-password becomes the askPassword signal; the
// credential dialog answers through providePassword()/cancelPassword().
// Server verification questions use askQuestion/answerQuestion; cancellation
// also goes through cancelPassword and never selects a trust decision.
// gvfs re-asks on a wrong password, so the dialog may fire several times
// for one mount.
class Mounter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Mounter(QObject *parent = nullptr);
    ~Mounter() override;

    // Mount whatever contains `location`, then report. An already-mounted
    // location succeeds immediately — callers need not check first.
    Q_INVOKABLE void mountLocation(const QString &location);

    // Answers to askPassword. `remember` saves permanently (the keyring);
    // otherwise the credential lives only as long as the mount does.
    Q_INVOKABLE void providePassword(const QString &username, const QString &domain,
                                     const QString &password, bool anonymous, bool remember);
    Q_INVOKABLE void cancelPassword();
    Q_INVOKABLE void answerQuestion(int choice);

    // A fresh operation wired to this Mounter's dialog signals, for callers
    // that run their own mount calls (PlacesModel). Caller owns the ref.
    GMountOperation *createOperation();

Q_SIGNALS:
    void mounted(const QString &location);
    void mountFailed(const QString &location, const QString &message);
    void promptAborted();
    void askQuestion(const QString &message, const QStringList &choices);
    void askPassword(const QString &message, const QString &defaultUser,
                     const QString &defaultDomain, bool needsUsername, bool needsDomain,
                     bool needsPassword, bool canAnonymous);

private:
    struct MountCtx {
        QPointer<Mounter> self;
        QString location;
    };

    static void onAskPassword(GMountOperation *operation, const char *message,
                              const char *defaultUser, const char *defaultDomain,
                              GAskPasswordFlags flags, gpointer data);
    static void onAskQuestion(GMountOperation *operation, const char *message,
                              const char *const *choices, gpointer data);
    static void onAborted(GMountOperation *operation, gpointer data);
    static void onMountReady(GObject *source, GAsyncResult *res, gpointer data);

    // The operation currently waiting on the dialog, if any.
    GMountOperation *m_pending = nullptr;
    int m_questionChoices = 0;
};
