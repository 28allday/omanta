#pragma once

#include "Settings.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

class QProcess;

// The in-app face of omanta-switch: whether omanta is the default file
// manager, and whether the Omarchy Toggle menu (SUPER+CTRL+O) carries the row
// that flips it. The script stays the one place that knows how to switch —
// the same command a person can run by hand — and this only runs it and
// reads back its status.
class DefaultFileManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // False when the switcher is not installed; the UI then hides itself.
    Q_PROPERTY(bool available READ available CONSTANT)
    // An Omarchy desktop: only there does the Toggle menu exist.
    Q_PROPERTY(bool omarchy READ omarchy CONSTANT)
    Q_PROPERTY(bool isDefault READ isDefault NOTIFY statusChanged)
    Q_PROPERTY(bool menuInstalled READ menuInstalled NOTIFY statusChanged)
    // True from the first status read on; the switches mean nothing before.
    Q_PROPERTY(bool known READ known NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

public:
    explicit DefaultFileManager(QObject *parent = nullptr);

    bool available() const { return !m_program.isEmpty(); }
    bool omarchy() const;
    bool isDefault() const { return m_isDefault; }
    bool menuInstalled() const { return m_menuInstalled; }
    bool known() const { return m_known; }
    bool busy() const { return m_busy; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setDefault(bool omanta);
    Q_INVOKABLE void setMenuInstalled(bool installed);

    // First launch on Omarchy: add the Toggle-menu row, once. Only the row —
    // which file manager is the default stays the person's choice — and a
    // row they later remove is never put back.
    Q_INVOKABLE void offerToggleMenu(Settings *settings);

Q_SIGNALS:
    void statusChanged();
    void busyChanged();

private:
    // Commands queue behind each other: the script edits files, and two
    // copies racing over bindings.lua would be the one way to corrupt it.
    void run(const QStringList &arguments);
    void startNext();
    void parseStatus(const QString &output);
    void setBusy(bool busy);

    QString m_program;
    QList<QStringList> m_queue;
    QProcess *m_process = nullptr;
    bool m_isDefault = false;
    bool m_menuInstalled = false;
    bool m_known = false;
    bool m_busy = false;
    QString m_lastError;
};
