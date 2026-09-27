#include "Previewer.h"
#include "Location.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QUrl>

namespace {

constexpr auto kPath = "/org/gnome/NautilusPreviewer";
constexpr auto kInterface = "org.gnome.NautilusPreviewer2";
constexpr auto kProperties = "org.freedesktop.DBus.Properties";

} // namespace

Previewer::Previewer(QObject *parent)
    : QObject(parent)
    , m_service(qEnvironmentVariable("OMANTA_PREVIEWER_SERVICE",
                                     QStringLiteral("org.gnome.NautilusPreviewer")))
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    bus.connect(m_service, QLatin1String(kPath), QLatin1String(kProperties),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    // Sushi 50 introspects SelectionEvent as (q) but sends (u) — seen live —
    // and QtDBus delivers only an exact signature match. Take both.
    bus.connect(m_service, QLatin1String(kPath), QLatin1String(kInterface),
                QStringLiteral("SelectionEvent"), this, SLOT(selectionEventReceived(uint)));
    bus.connect(m_service, QLatin1String(kPath), QLatin1String(kInterface),
                QStringLiteral("SelectionEvent"), this, SLOT(selectionEventReceived16(ushort)));
    // Sushi quitting or crashing takes its window with it, announced or not.
    m_watcher = new QDBusServiceWatcher(m_service, bus,
                                        QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { setVisible(false); });
}

bool Previewer::available() const
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusConnectionInterface *daemon = bus.interface();
    if (daemon->isServiceRegistered(m_service))
        return true;
    const QDBusReply<QStringList> startable = daemon->activatableServiceNames();
    return startable.isValid() && startable.value().contains(m_service);
}

bool Previewer::show(const QString &location, bool toggle)
{
    // Up already means installed: no bus round trips per selection change.
    if (location.isEmpty() || (!m_visible && !available()))
        return false;
    const QString uri = Location::isUri(location)
        ? location : QUrl::fromLocalFile(location).toString(QUrl::FullyEncoded);
    // (ssbs): uri, parent window handle, close-if-shown, activation token.
    // No handle — Omarchy floats the previewer by its own window rule — and
    // no token: it is focused regardless. Sushi 50 rejects the older (ssb).
    QDBusMessage call = QDBusMessage::createMethodCall(m_service, QLatin1String(kPath),
                                                       QLatin1String(kInterface),
                                                       QStringLiteral("ShowFile"));
    call << uri << QString() << toggle << QString();
    // A rejected call (a changed signature, a broken previewer) leaves no
    // window — without this the selection would keep asking for one.
    auto *reply = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(reply, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *done) {
        if (done->isError())
            setVisible(false);
        done->deleteLater();
    });
    // Toggling a preview that is up closes it; anything else leaves one up.
    setVisible(!(toggle && m_visible));
    return true;
}

void Previewer::setOwner(QObject *owner)
{
    if (m_owner == owner)
        return;
    m_owner = owner;
    Q_EMIT ownerChanged();
}

void Previewer::selectionEventReceived(uint direction)
{
    Q_EMIT selectionEvent(int(direction));
}

void Previewer::selectionEventReceived16(ushort direction)
{
    Q_EMIT selectionEvent(direction);
}

void Previewer::close()
{
    if (!m_visible)
        return;
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
        m_service, QLatin1String(kPath), QLatin1String(kInterface), QStringLiteral("Close")));
    setVisible(false);
}

void Previewer::propertiesChanged(const QString &interface, const QVariantMap &changed,
                                  const QStringList &)
{
    if (interface == QLatin1String(kInterface) && changed.contains(QStringLiteral("Visible")))
        setVisible(changed.value(QStringLiteral("Visible")).toBool());
}

void Previewer::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    Q_EMIT visibleChanged();
}
