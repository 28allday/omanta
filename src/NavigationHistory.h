#pragma once

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration>

// Back/forward for a single tab. Browser semantics: navigating somewhere new
// truncates whatever was ahead of you.
class NavigationHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString current READ current NOTIFY currentChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY currentChanged)

public:
    explicit NavigationHistory(QObject *parent = nullptr);

    QString current() const;
    bool canGoBack() const { return m_position > 0; }
    bool canGoForward() const { return m_position >= 0 && m_position < m_entries.size() - 1; }

    // Pushes a new location. Re-navigating to where you already are is a no-op
    // rather than a duplicate entry, so Back never appears to do nothing.
    Q_INVOKABLE void visit(const QString &path);
    Q_INVOKABLE QString goBack();
    Q_INVOKABLE QString goForward();
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void currentChanged();

private:
    QStringList m_entries;
    int m_position = -1;
};
