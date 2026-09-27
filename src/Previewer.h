#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration>

// Quick Look: Space previews the selected file in Sushi, the previewer
// Nautilus uses and stock Omarchy ships. It is a separate D-Bus service, so
// omanta gets the same previews (zoomable images, text, PDF, office files
// when LibreOffice is present, audio, video) by asking it the way Nautilus
// does — org.gnome.NautilusPreviewer2.ShowFile — rather than growing viewers
// of its own.
//
// OMANTA_PREVIEWER_SERVICE names another bus name (the tests register a fake
// there, so a suite run never opens a window).
class Previewer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Whether the preview window is up. Sushi 50 announces only closing
    // (PropertiesChanged Visible=false) and its Visible getter fails, so an
    // open is recorded from our own ShowFile. While up, the preview follows
    // the selection of its owner — the tab that opened it.
    Q_PROPERTY(bool visible READ isVisible NOTIFY visibleChanged)
    Q_PROPERTY(QObject *owner READ owner WRITE setOwner NOTIFY ownerChanged)

public:
    explicit Previewer(QObject *parent = nullptr);

    bool isVisible() const { return m_visible; }
    QObject *owner() const { return m_owner; }
    void setOwner(QObject *owner);

    // Installed (running, or startable by the bus)?
    Q_INVOKABLE bool available() const;
    // Shows `location` (a path or URI). With `toggle`, a preview already up
    // closes instead — Space again. False when there is no previewer.
    Q_INVOKABLE bool show(const QString &location, bool toggle);
    Q_INVOKABLE void close();

Q_SIGNALS:
    void visibleChanged();
    void ownerChanged();
    // An arrow key pressed in the preview window (a GtkDirectionType: 0/1
    // tab forward/back, 2 up, 3 down, 4 left, 5 right) — step the owner's
    // selection, as Nautilus does.
    void selectionEvent(int direction);

private Q_SLOTS:
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated);
    void selectionEventReceived(uint direction);
    void selectionEventReceived16(ushort direction);

private:
    void setVisible(bool visible);

    QString m_service;
    bool m_visible = false;
    QPointer<QObject> m_owner;
    class QDBusServiceWatcher *m_watcher = nullptr;
};
