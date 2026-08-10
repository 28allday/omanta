#include "NavigationHistory.h"

NavigationHistory::NavigationHistory(QObject *parent)
    : QObject(parent)
{
}

QString NavigationHistory::current() const
{
    if (m_position < 0 || m_position >= m_entries.size())
        return {};
    return m_entries.at(m_position);
}

void NavigationHistory::visit(const QString &path)
{
    if (path.isEmpty() || path == current())
        return;

    // Anything ahead of the cursor is a branch we just abandoned.
    while (m_entries.size() > m_position + 1)
        m_entries.removeLast();

    m_entries.append(path);
    m_position = int(m_entries.size()) - 1;
    Q_EMIT currentChanged();
}

QString NavigationHistory::goBack()
{
    if (!canGoBack())
        return {};
    --m_position;
    Q_EMIT currentChanged();
    return current();
}

QString NavigationHistory::goForward()
{
    if (!canGoForward())
        return {};
    ++m_position;
    Q_EMIT currentChanged();
    return current();
}

void NavigationHistory::clear()
{
    m_entries.clear();
    m_position = -1;
    Q_EMIT currentChanged();
}
