#include "FileChooserPortal.h"

#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDir>
#include <QFileInfo>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

namespace {

constexpr const char *kFileChooserInterface = "org.freedesktop.impl.portal.FileChooser";
constexpr const char *kRequestInterface = "org.freedesktop.impl.portal.Request";
constexpr const char *kPickerUrl = "qrc:/qt/qml/Omanta/qml/FilePicker.qml";

// The portal's response codes.
constexpr uint kResponseSuccess = 0;
constexpr uint kResponseCancelled = 1;
constexpr uint kResponseOther = 2;

// canConvert<QDBusArgument>() says yes to plain values too (a QByteArray
// among them) and then hands back an empty argument; ask what it holds.
bool isArgument(const QVariant &value)
{
    return value.metaType() == QMetaType::fromType<QDBusArgument>();
}

template<typename T>
T demarshal(const QVariant &value)
{
    if (isArgument(value))
        return qdbus_cast<T>(value.value<QDBusArgument>());
    return value.value<T>();
}

// Paths travel as bytestrings ("ay"), NUL-terminated by convention.
QString bytePath(const QVariant &value)
{
    QByteArray bytes;
    if (isArgument(value))
        value.value<QDBusArgument>() >> bytes;
    else
        bytes = value.toByteArray();
    while (bytes.endsWith('\0'))
        bytes.chop(1);
    return QFile::decodeName(bytes);
}

QStringList bytePaths(const QVariant &value)
{
    QStringList paths;
    if (!isArgument(value))
        return paths;
    const QDBusArgument argument = value.value<QDBusArgument>();
    argument.beginArray();
    while (!argument.atEnd()) {
        QByteArray bytes;
        argument >> bytes;
        paths.append(bytePath(bytes));
    }
    argument.endArray();
    return paths;
}

bool sameFilter(const PortalFilter &a, const PortalFilter &b)
{
    if (a.name != b.name || a.rules.size() != b.rules.size())
        return false;
    for (qsizetype i = 0; i < a.rules.size(); ++i) {
        if (a.rules[i].type != b.rules[i].type || a.rules[i].pattern != b.rules[i].pattern)
            return false;
    }
    return true;
}

// "Firefox" for org.mozilla.firefox — what the picker's caption names.
// Host apps often arrive with no id at all; then there is no name to show.
QString applicationName(const QString &appId)
{
    if (appId.isEmpty())
        return {};
    const QByteArray desktopId = (appId + QStringLiteral(".desktop")).toUtf8();
    GDesktopAppInfo *info = g_desktop_app_info_new(desktopId.constData());
    if (!info)
        return {};
    const QString name = QString::fromUtf8(g_app_info_get_display_name(G_APP_INFO(info)));
    g_object_unref(info);
    return name;
}

// What the app gets back: file:// wherever the file has a local path (gvfs
// mounts included, through their FUSE path, as GTK does), the URI otherwise.
QString toUri(const QString &location)
{
    const QByteArray arg = location.toUtf8();
    GFile *file = g_file_new_for_commandline_arg(arg.constData());
    QString uri;
    if (char *path = g_file_get_path(file)) {
        uri = QUrl::fromLocalFile(QString::fromUtf8(path)).toString(QUrl::FullyEncoded);
        g_free(path);
    } else if (char *raw = g_file_get_uri(file)) {
        uri = QString::fromUtf8(raw);
        g_free(raw);
    }
    g_object_unref(file);
    return uri;
}

// A var array out of QML may arrive wrapped as a QJSValue.
QVariantList plainList(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return value.value<QJSValue>().toVariant().toList();
    return value.toList();
}

QVariantList filtersForQml(const PortalFilterList &filters)
{
    QVariantList list;
    for (const PortalFilter &filter : filters) {
        QVariantList rules;
        for (const PortalFilterRule &rule : filter.rules)
            rules.append(QVariantMap{ { QStringLiteral("type"), rule.type },
                                      { QStringLiteral("pattern"), rule.pattern } });
        list.append(QVariantMap{ { QStringLiteral("name"), filter.name },
                                 { QStringLiteral("rules"), rules } });
    }
    return list;
}

QVariantList choicesForQml(const PortalChoiceList &choices)
{
    QVariantList list;
    for (const PortalChoice &choice : choices) {
        QVariantList options;
        for (const PortalChoiceOption &option : choice.options)
            options.append(QVariantMap{ { QStringLiteral("id"), option.id },
                                        { QStringLiteral("label"), option.label } });
        list.append(QVariantMap{ { QStringLiteral("id"), choice.id },
                                 { QStringLiteral("label"), choice.label },
                                 { QStringLiteral("options"), options },
                                 { QStringLiteral("value"), choice.initial } });
    }
    return list;
}

} // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule)
{
    argument.beginStructure();
    argument << rule.type << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule)
{
    argument.beginStructure();
    argument >> rule.type >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilter &filter)
{
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilter &filter)
{
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoiceOption &option)
{
    argument.beginStructure();
    argument << option.id << option.label;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoiceOption &option)
{
    argument.beginStructure();
    argument >> option.id >> option.label;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoice &choice)
{
    argument.beginStructure();
    argument << choice.id << choice.label << choice.options << choice.initial;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoice &choice)
{
    argument.beginStructure();
    argument >> choice.id >> choice.label >> choice.options >> choice.initial;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoiceAnswer &answer)
{
    argument.beginStructure();
    argument << answer.id << answer.value;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoiceAnswer &answer)
{
    argument.beginStructure();
    argument >> answer.id >> answer.value;
    argument.endStructure();
    return argument;
}

FileChooserPortal::FileChooserPortal(QQmlEngine *engine, QObject *parent)
    : QDBusVirtualObject(parent)
    , m_engine(engine)
{
}

void FileChooserPortal::registerTypes()
{
    qDBusRegisterMetaType<PortalFilterRule>();
    qDBusRegisterMetaType<QList<PortalFilterRule>>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<PortalFilterList>();
    qDBusRegisterMetaType<PortalChoiceOption>();
    qDBusRegisterMetaType<QList<PortalChoiceOption>>();
    qDBusRegisterMetaType<PortalChoice>();
    qDBusRegisterMetaType<PortalChoiceList>();
    qDBusRegisterMetaType<PortalChoiceAnswer>();
    qDBusRegisterMetaType<PortalChoiceAnswerList>();
}

QString FileChooserPortal::introspect(const QString &path) const
{
    if (path != QLatin1String(ObjectPath)) {
        return QStringLiteral(
            "<interface name=\"org.freedesktop.impl.portal.Request\">"
            "<method name=\"Close\"/>"
            "</interface>");
    }

    const QString method = QStringLiteral(
        "<method name=\"%1\">"
        "<arg type=\"o\" name=\"handle\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"app_id\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"parent_window\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"title\" direction=\"in\"/>"
        "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>"
        "<arg type=\"u\" name=\"response\" direction=\"out\"/>"
        "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>"
        "</method>");
    return QStringLiteral("<interface name=\"org.freedesktop.impl.portal.FileChooser\">")
         + method.arg(QStringLiteral("OpenFile"))
         + method.arg(QStringLiteral("SaveFile"))
         + method.arg(QStringLiteral("SaveFiles"))
         + QStringLiteral("</interface>");
}

bool FileChooserPortal::handleMessage(const QDBusMessage &message, const QDBusConnection &connection)
{
    if (message.type() != QDBusMessage::MethodCallMessage)
        return false;

    if (message.interface() == QLatin1String(kRequestInterface)
        && message.member() == QLatin1String("Close")) {
        // The frontend gave up on the request (the app went away, or it
        // cancelled): take the window down; its finish answers the call.
        const auto it = m_pending.constFind(message.path());
        if (it != m_pending.cend() && it->window)
            QMetaObject::invokeMethod(it->window, "abandon");
        connection.send(message.createReply());
        return true;
    }

    if (message.interface() != QLatin1String(kFileChooserInterface))
        return false;

    const QString member = message.member();
    if (member != QLatin1String("OpenFile") && member != QLatin1String("SaveFile")
        && member != QLatin1String("SaveFiles"))
        return false;

    if (message.signature() != QLatin1String("osssa{sv}")) {
        connection.send(message.createErrorReply(QDBusError::InvalidArgs,
                                                 QStringLiteral("expected (osssa{sv})")));
        return true;
    }

    startRequest(message, connection);
    return true;
}

void FileChooserPortal::startRequest(const QDBusMessage &message, const QDBusConnection &connection)
{
    const QList<QVariant> args = message.arguments();
    const QString handle = args.at(0).value<QDBusObjectPath>().path();
    const QString appId = args.at(1).toString();
    const QString title = args.at(3).toString();
    // Held for the whole function: byte strings ("ay") come out of the
    // argument pointing into its buffer, so they are only good while it
    // lives. bytePath()/bytePaths() copy them out before it goes.
    const QDBusArgument optionsArgument = args.at(4).value<QDBusArgument>();
    QVariantMap options;
    optionsArgument >> options;

    const QString member = message.member();
    const QString mode = member == QLatin1String("OpenFile") ? QStringLiteral("open")
                       : member == QLatin1String("SaveFile") ? QStringLiteral("save")
                                                             : QStringLiteral("saveFiles");

    // A second request on a live handle would orphan the first call.
    if (handle.isEmpty() || m_pending.contains(handle)) {
        connection.send(message.createErrorReply(QDBusError::InvalidArgs,
                                                 QStringLiteral("bad request handle")));
        return;
    }

    PortalFilterList filters = demarshal<PortalFilterList>(options.value(QStringLiteral("filters")));
    int filterIndex = filters.isEmpty() ? -1 : 0;
    if (options.contains(QStringLiteral("current_filter"))) {
        // The filter to start on; when it is not one of the list (or there
        // is no list), it is the only filter there is.
        const PortalFilter current = demarshal<PortalFilter>(options.value(QStringLiteral("current_filter")));
        filterIndex = -1;
        for (qsizetype i = 0; i < filters.size(); ++i) {
            if (sameFilter(filters[i], current)) {
                filterIndex = int(i);
                break;
            }
        }
        if (filterIndex < 0 && !current.rules.isEmpty()) {
            filters.append(current);
            filterIndex = int(filters.size() - 1);
        }
    }

    const PortalChoiceList choices = demarshal<PortalChoiceList>(options.value(QStringLiteral("choices")));

    QString folder = bytePath(options.value(QStringLiteral("current_folder")));
    QString name = options.value(QStringLiteral("current_name")).toString();
    const QString file = bytePath(options.value(QStringLiteral("current_file")));
    if (!file.isEmpty()) {
        // Save over an existing file: start beside it with its name filled in.
        const QFileInfo info(file);
        folder = info.absolutePath();
        name = info.fileName();
    }
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        folder = QDir::homePath();

    QVariantMap request{
        { QStringLiteral("mode"), mode },
        { QStringLiteral("title"), title },
        { QStringLiteral("appName"), applicationName(appId) },
        { QStringLiteral("acceptLabel"), options.value(QStringLiteral("accept_label")).toString() },
        { QStringLiteral("multiple"), options.value(QStringLiteral("multiple")).toBool() },
        { QStringLiteral("directory"), options.value(QStringLiteral("directory")).toBool() },
        { QStringLiteral("filters"), filtersForQml(filters) },
        { QStringLiteral("filterIndex"), filterIndex },
        { QStringLiteral("choices"), choicesForQml(choices) },
        { QStringLiteral("folder"), folder },
        { QStringLiteral("currentName"), name },
        { QStringLiteral("files"), bytePaths(options.value(QStringLiteral("files"))) },
    };

    QQmlComponent component(m_engine, QUrl(QLatin1String(kPickerUrl)));
    QObject *window = component.isError()
        ? nullptr
        : component.createWithInitialProperties({ { QStringLiteral("request"), request } });
    if (!window) {
        for (const QQmlError &error : component.errors())
            qCritical("omanta: %s", qUtf8Printable(error.toString()));
        connection.send(message.createErrorReply(QDBusError::Failed,
                                                 QStringLiteral("could not open the file chooser")));
        return;
    }
    // C++ owns it, as with the browser windows: nothing in JavaScript holds
    // a reference, so the garbage collector would take it from under us.
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    window->setParent(this);

    message.setDelayedReply(true);
    m_pending.insert(handle, Pending{ message, connection.name(), window, filters });

    connect(window, SIGNAL(finished(bool,QVariant,int,QVariant)),
            this, SLOT(windowFinished(bool,QVariant,int,QVariant)));

    if (auto *quickWindow = qobject_cast<QQuickWindow *>(window)) {
        quickWindow->show();
        quickWindow->raise();
        quickWindow->requestActivate();
    }
}

void FileChooserPortal::windowFinished(bool accepted, const QVariant &paths, int filterIndex,
                                       const QVariant &choiceValues)
{
    QObject *window = sender();
    QString handle;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (it->window == window) {
            handle = it.key();
            break;
        }
    }
    if (handle.isEmpty())
        return;

    if (!accepted) {
        reply(handle, kResponseCancelled, {});
        return;
    }

    QStringList uris;
    for (const QVariant &path : plainList(paths)) {
        const QString uri = toUri(path.toString());
        if (!uri.isEmpty())
            uris.append(uri);
    }
    if (uris.isEmpty()) {
        reply(handle, kResponseOther, {});
        return;
    }

    QVariantMap results{
        { QStringLiteral("uris"), uris },
        // The user chose these through the dialog, which is the consent a
        // sandboxed app needs to write to them.
        { QStringLiteral("writable"), true },
    };

    const PortalFilterList &filters = m_pending.value(handle).filters;
    if (filterIndex >= 0 && filterIndex < filters.size())
        results.insert(QStringLiteral("current_filter"), QVariant::fromValue(filters.at(filterIndex)));

    PortalChoiceAnswerList answers;
    for (const QVariant &entry : plainList(choiceValues)) {
        const QVariantMap map = entry.toMap();
        answers.append({ map.value(QStringLiteral("id")).toString(),
                         map.value(QStringLiteral("value")).toString() });
    }
    if (!answers.isEmpty())
        results.insert(QStringLiteral("choices"), QVariant::fromValue(answers));

    reply(handle, kResponseSuccess, results);
}

void FileChooserPortal::reply(const QString &handle, uint response, const QVariantMap &results)
{
    const Pending pending = m_pending.take(handle);
    QDBusConnection connection(pending.connectionName);
    connection.send(pending.call.createReply(
        QVariantList{ QVariant::fromValue(response), QVariant::fromValue(results) }));

    if (pending.window)
        pending.window->deleteLater();
    if (m_pending.isEmpty())
        Q_EMIT idle();
}
