#include "Mounter.h"
#include "Location.h"

#include <QPointer>

Mounter::Mounter(QObject *parent)
    : QObject(parent)
{
}

Mounter::~Mounter()
{
    if (m_pending) {
        g_mount_operation_reply(m_pending, G_MOUNT_OPERATION_ABORTED);
        g_object_unref(m_pending);
    }
}

GMountOperation *Mounter::createOperation()
{
    // The operation can outlive this Mounter — the async mount holds it. The
    // handler data is a guarded pointer freed with the connection, so a
    // question arriving after the window closed is dropped, not dispatched
    // into freed memory.
    GMountOperation *operation = g_mount_operation_new();
    auto *guard = new QPointer<Mounter>(this);
    g_signal_connect_data(operation, "ask-password", G_CALLBACK(&Mounter::onAskPassword), guard,
                          [](gpointer data, GClosure *) {
                              delete static_cast<QPointer<Mounter> *>(data);
                          },
                          GConnectFlags(0));
    auto *questionGuard = new QPointer<Mounter>(this);
    g_signal_connect_data(operation, "ask-question", G_CALLBACK(&Mounter::onAskQuestion), questionGuard,
                          [](gpointer data, GClosure *) {
                              delete static_cast<QPointer<Mounter> *>(data);
                          }, GConnectFlags(0));
    auto *abortGuard = new QPointer<Mounter>(this);
    g_signal_connect_data(operation, "aborted", G_CALLBACK(&Mounter::onAborted), abortGuard,
                          [](gpointer data, GClosure *) {
                              delete static_cast<QPointer<Mounter> *>(data);
                          }, GConnectFlags(0));
    return operation;
}

void Mounter::onAborted(GMountOperation *operation, gpointer data)
{
    auto *guard = static_cast<QPointer<Mounter> *>(data);
    Mounter *self = guard->data();
    if (!self || self->m_pending != operation)
        return;
    // The backend has already aborted. Release only its matching prompt,
    // without replying again or interfering with another mount's dialog.
    self->m_questionChoices = 0;
    g_clear_object(&self->m_pending);
    Q_EMIT self->promptAborted();
}

void Mounter::onAskPassword(GMountOperation *operation, const char *message,
                            const char *defaultUser, const char *defaultDomain,
                            GAskPasswordFlags flags, gpointer data)
{
    // GMountOperation's default handler schedules an UNHANDLED reply in idle.
    // Our asynchronous dialog owns the answer; do not let that handler race it.
    g_signal_stop_emission_by_name(operation, "ask-password");
    auto *guard = static_cast<QPointer<Mounter> *>(data);
    Mounter *self = guard->data();
    if (!self) {
        g_mount_operation_reply(operation, G_MOUNT_OPERATION_ABORTED);
        return;
    }

    // Only one question at a time reaches the dialog. A second mount asking
    // while the first waits is aborted rather than silently queued behind a
    // dialog the user thinks is about something else.
    if (self->m_pending) {
        g_mount_operation_reply(operation, G_MOUNT_OPERATION_ABORTED);
        return;
    }

    self->m_pending = G_MOUNT_OPERATION(g_object_ref(operation));
    Q_EMIT self->askPassword(QString::fromUtf8(message ? message : ""),
                             QString::fromUtf8(defaultUser ? defaultUser : ""),
                             QString::fromUtf8(defaultDomain ? defaultDomain : ""),
                             (flags & G_ASK_PASSWORD_NEED_USERNAME) != 0,
                             (flags & G_ASK_PASSWORD_NEED_DOMAIN) != 0,
                             (flags & G_ASK_PASSWORD_NEED_PASSWORD) != 0,
                             (flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED) != 0);
}

void Mounter::onAskQuestion(GMountOperation *operation, const char *message,
                             const char *const *choices, gpointer data)
{
    g_signal_stop_emission_by_name(operation, "ask-question");
    auto *guard = static_cast<QPointer<Mounter> *>(data);
    Mounter *self = guard->data();
    if (!self || self->m_pending || !choices || !choices[0]) {
        g_mount_operation_reply(operation, G_MOUNT_OPERATION_ABORTED);
        return;
    }
    QStringList options;
    for (int i = 0; choices[i]; ++i)
        options << QString::fromUtf8(choices[i]);
    self->m_pending = G_MOUNT_OPERATION(g_object_ref(operation));
    self->m_questionChoices = options.size();
    Q_EMIT self->askQuestion(QString::fromUtf8(message ? message : ""), options);
}

void Mounter::answerQuestion(int choice)
{
    if (!m_pending || choice < 0 || choice >= m_questionChoices)
        return;
    g_mount_operation_set_choice(m_pending, choice);
    GMountOperation *pending = m_pending;
    m_pending = nullptr;
    m_questionChoices = 0;
    g_mount_operation_reply(pending, G_MOUNT_OPERATION_HANDLED);
    g_object_unref(pending);
}

void Mounter::providePassword(const QString &username, const QString &domain,
                              const QString &password, bool anonymous, bool remember)
{
    if (!m_pending || m_questionChoices != 0)
        return;

    if (anonymous) {
        g_mount_operation_set_anonymous(m_pending, TRUE);
    } else {
        if (!username.isEmpty())
            g_mount_operation_set_username(m_pending, username.toUtf8().constData());
        if (!domain.isEmpty())
            g_mount_operation_set_domain(m_pending, domain.toUtf8().constData());
        g_mount_operation_set_password(m_pending, password.toUtf8().constData());
        g_mount_operation_set_password_save(m_pending, remember ? G_PASSWORD_SAVE_PERMANENTLY
                                                                : G_PASSWORD_SAVE_NEVER);
    }

    GMountOperation *pending = m_pending;
    m_pending = nullptr;
    g_mount_operation_reply(pending, G_MOUNT_OPERATION_HANDLED);
    g_object_unref(pending);
}

void Mounter::cancelPassword()
{
    if (!m_pending)
        return;
    GMountOperation *pending = m_pending;
    m_pending = nullptr;
    m_questionChoices = 0;
    g_mount_operation_reply(pending, G_MOUNT_OPERATION_ABORTED);
    g_object_unref(pending);
}

void Mounter::mountLocation(const QString &location)
{
    GFile *file = Location::make(location);
    GMountOperation *operation = createOperation();

    auto *ctx = new MountCtx{ this, Location::clean(location) };
    g_file_mount_enclosing_volume(file, G_MOUNT_MOUNT_NONE, operation, nullptr,
                                  &Mounter::onMountReady, ctx);
    g_object_unref(operation); // the async call holds its own ref
    g_object_unref(file);
}

void Mounter::onMountReady(GObject *source, GAsyncResult *res, gpointer data)
{
    auto *ctx = static_cast<MountCtx *>(data);
    GError *error = nullptr;
    const bool ok = g_file_mount_enclosing_volume_finish(G_FILE(source), res, &error);

    if (!ctx->self) {
        g_clear_error(&error);
        delete ctx;
        return;
    }

    // Already mounted is what the caller wanted all along.
    if (ok || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED)) {
        Q_EMIT ctx->self->mounted(ctx->location);
    } else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED)
               && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        Q_EMIT ctx->self->mountFailed(
            ctx->location,
            error ? QString::fromUtf8(error->message) : QStringLiteral("mount failed"));
    } else {
        // Aborted from the dialog: the user said no; not an error to show.
        Q_EMIT ctx->self->mountFailed(ctx->location, QString());
    }

    g_clear_error(&error);
    delete ctx;
}
