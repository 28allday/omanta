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
    return operation;
}

void Mounter::onAskPassword(GMountOperation *operation, const char *message,
                            const char *defaultUser, const char *defaultDomain,
                            GAskPasswordFlags flags, gpointer data)
{
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

void Mounter::providePassword(const QString &username, const QString &domain,
                              const QString &password, bool anonymous, bool remember)
{
    if (!m_pending)
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

    g_mount_operation_reply(m_pending, G_MOUNT_OPERATION_HANDLED);
    g_clear_object(&m_pending);
}

void Mounter::cancelPassword()
{
    if (!m_pending)
        return;
    g_mount_operation_reply(m_pending, G_MOUNT_OPERATION_ABORTED);
    g_clear_object(&m_pending);
}

void Mounter::mountLocation(const QString &location)
{
    GFile *file = Location::make(location);
    GMountOperation *operation = createOperation();

    auto *ctx = new MountCtx{ this, location };
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
