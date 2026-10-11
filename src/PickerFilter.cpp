#include "PickerFilter.h"

#include <gio/gio.h>

PickerFilter::PickerFilter(QObject *parent)
    : QObject(parent)
{
}

void PickerFilter::setRules(const QVariantList &rules)
{
    if (m_rules == rules)
        return;
    m_rules = rules;
    m_globs.clear();
    m_mimeTypes.clear();

    for (const QVariant &entry : rules) {
        const QVariantMap rule = entry.toMap();
        const QString pattern = rule.value(QStringLiteral("pattern")).toString();
        if (pattern.isEmpty())
            continue;
        if (rule.value(QStringLiteral("type")).toUInt() == 1) {
            m_mimeTypes.append(pattern);
        } else {
            // GTK matches suffix globs without regard to case, so "*.jpg"
            // must take IMG_0001.JPG straight off a camera card.
            m_globs.append(QRegularExpression(
                QRegularExpression::wildcardToRegularExpression(pattern),
                QRegularExpression::CaseInsensitiveOption));
        }
    }

    ++m_revision;
    Q_EMIT rulesChanged();
}

bool PickerFilter::matches(const QString &name, const QString &contentType, bool isDir) const
{
    if (isDir || (m_globs.isEmpty() && m_mimeTypes.isEmpty()))
        return true;

    for (const QRegularExpression &glob : m_globs) {
        if (glob.match(name).hasMatch())
            return true;
    }

    if (contentType.isEmpty())
        return false;
    const QByteArray type = contentType.toUtf8();
    for (const QString &mime : m_mimeTypes) {
        if (mime == QLatin1String("*/*") || mime == QLatin1String("*"))
            return true;
        if (mime.endsWith(QLatin1String("/*"))) {
            // "image/*": the family, by prefix — g_content_type_is_a has no
            // wildcard form.
            if (contentType.startsWith(mime.chopped(1)))
                return true;
            continue;
        }
        // is_a, not equality: a filter for text/plain takes text/x-csrc,
        // as every other chooser does.
        if (g_content_type_is_a(type.constData(), mime.toUtf8().constData()))
            return true;
    }
    return false;
}
