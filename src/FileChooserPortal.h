#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QPointer>
#include <QString>
#include <QVariant>

class QQmlEngine;

// The wire shapes of org.freedesktop.impl.portal.FileChooser that Qt cannot
// marshal on its own. A filter is (sa(us)): a name and its rules, each rule a
// type (0 glob, 1 MIME type) and a pattern. A choice is (ssa(ss)s): an id, a
// label, its options (none means a checkbox) and the initial value; the
// answer comes back as a(ss), id and value.
struct PortalFilterRule
{
    uint type = 0;
    QString pattern;
};

struct PortalFilter
{
    QString name;
    QList<PortalFilterRule> rules;
};
using PortalFilterList = QList<PortalFilter>;

struct PortalChoiceOption
{
    QString id;
    QString label;
};

struct PortalChoice
{
    QString id;
    QString label;
    QList<PortalChoiceOption> options;
    QString initial;
};
using PortalChoiceList = QList<PortalChoice>;

struct PortalChoiceAnswer
{
    QString id;
    QString value;
};
using PortalChoiceAnswerList = QList<PortalChoiceAnswer>;

Q_DECLARE_METATYPE(PortalFilterRule)
Q_DECLARE_METATYPE(PortalFilter)
Q_DECLARE_METATYPE(PortalFilterList)
Q_DECLARE_METATYPE(PortalChoiceOption)
Q_DECLARE_METATYPE(PortalChoice)
Q_DECLARE_METATYPE(PortalChoiceList)
Q_DECLARE_METATYPE(PortalChoiceAnswer)
Q_DECLARE_METATYPE(PortalChoiceAnswerList)

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule);
QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilter &filter);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilter &filter);
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoiceOption &option);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoiceOption &option);
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoice &choice);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoice &choice);
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoiceAnswer &answer);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoiceAnswer &answer);

// omanta as the desktop's file chooser: the backend half of the
// xdg-desktop-portal FileChooser portal. Apps (browsers, Electron, Qt and
// GTK 4 apps) ask the portal frontend for an Open or Save dialog; the
// frontend forwards the request here when the user's portals.conf names
// omanta for org.freedesktop.impl.portal.FileChooser, and this shows
// omanta's own picker window instead of the GTK one.
//
// A virtual object rather than an adaptor: the calls carry the struct types
// above inside a{sv}, the reply has to wait for the user (a delayed reply),
// and the frontend's Request.Close arrives on per-request handle paths below
// this object's path — all of which want the raw message.
class FileChooserPortal : public QDBusVirtualObject
{
    Q_OBJECT

public:
    static constexpr const char *ServiceName = "org.freedesktop.impl.portal.desktop.omanta";
    static constexpr const char *ObjectPath = "/org/freedesktop/portal/desktop";

    explicit FileChooserPortal(QQmlEngine *engine, QObject *parent = nullptr);

    // Registers the D-Bus struct types. Once per process, before any call.
    static void registerTypes();

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;
    QString introspect(const QString &path) const override;

    int pendingCount() const { return int(m_pending.size()); }

Q_SIGNALS:
    // A request was answered and none is left open.
    void idle();

private Q_SLOTS:
    void windowFinished(bool accepted, const QVariant &paths, int filterIndex,
                        const QVariant &choiceValues);

private:
    struct Pending
    {
        QDBusMessage call;
        // By name: QDBusConnection has no default constructor for the hash.
        QString connectionName;
        QPointer<QObject> window;
        PortalFilterList filters;
    };

    void startRequest(const QDBusMessage &message, const QDBusConnection &connection);
    void reply(const QString &handle, uint response, const QVariantMap &results);

    QQmlEngine *m_engine;
    QHash<QString, Pending> m_pending;
};
