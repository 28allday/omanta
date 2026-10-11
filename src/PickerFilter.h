#pragma once

#include <QObject>
#include <QRegularExpression>
#include <QVariantList>
#include <QtQmlIntegration>

// One file-type filter of the file chooser ("Images", "PDF document"), as
// the portal hands it over: a list of rules, each a glob (type 0, "*.jpg")
// or a MIME type (type 1, "image/png", "image/*"). Rows that match none of
// them are drawn dimmed and cannot be picked; folders always match, or there
// would be no way to walk to the file. No rules means everything matches.
class PickerFilter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList rules READ rules WRITE setRules NOTIFY rulesChanged)
    // Bumped on every rules change. Delegates read it in their binding, so a
    // filter switch re-evaluates every row without the views knowing why.
    Q_PROPERTY(int revision READ revision NOTIFY rulesChanged)

public:
    explicit PickerFilter(QObject *parent = nullptr);

    QVariantList rules() const { return m_rules; }
    void setRules(const QVariantList &rules);

    int revision() const { return m_revision; }

    Q_INVOKABLE bool matches(const QString &name, const QString &contentType, bool isDir) const;

Q_SIGNALS:
    void rulesChanged();

private:
    QVariantList m_rules;
    QList<QRegularExpression> m_globs;
    QStringList m_mimeTypes;
    int m_revision = 0;
};
