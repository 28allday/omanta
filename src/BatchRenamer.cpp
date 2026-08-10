#include "BatchRenamer.h"

#include <QHash>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

namespace {

// The exact strings Nautilus puts in the entry when a tag is added.
const QString kTagOriginal = QStringLiteral("[Original file name]");
const QString kTagNumber = QStringLiteral("[1, 2, 3]");
const QString kTagNumber2 = QStringLiteral("[01, 02, 03]");
const QString kTagNumber3 = QStringLiteral("[001, 002, 003]");

bool validName(const QString &name)
{
    return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..")
        && !name.contains(QLatin1Char('/'));
}

} // namespace

BatchRenamer::BatchRenamer(QObject *parent)
    : QObject(parent)
{
    // The proxy's ordering rules: numeric, case-insensitive.
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);

    m_templateText = kTagOriginal;
}

int BatchRenamer::extensionOffset(const QString &name, bool isDir)
{
    if (isDir)
        return -1;

    // ".tar.gz" and friends rename as one unit, as eel does in Nautilus.
    static const QStringList compound = {
        QStringLiteral(".tar.gz"), QStringLiteral(".tar.xz"), QStringLiteral(".tar.bz2"),
        QStringLiteral(".tar.zst"), QStringLiteral(".tar.Z"),
    };
    for (const QString &suffix : compound) {
        if (name.length() > suffix.length() && name.endsWith(suffix, Qt::CaseInsensitive))
            return name.length() - suffix.length();
    }

    // Last dot, but never a leading one: ".bashrc" is a name, not an extension.
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? dot : -1;
}

void BatchRenamer::setSelection(const QVariantList &items, const QStringList &existingNames)
{
    m_items.clear();
    m_items.reserve(items.size());
    for (const QVariant &value : items) {
        const QVariantMap map = value.toMap();
        Item item;
        item.path = map.value(QStringLiteral("path")).toString();
        item.name = map.value(QStringLiteral("name")).toString();
        item.modified = map.value(QStringLiteral("modified")).toDateTime();
        item.isDir = map.value(QStringLiteral("isDir")).toBool();
        m_items.append(item);
    }
    m_existingNames = existingNames;
    recompute();
}

void BatchRenamer::setMode(int mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    Q_EMIT inputsChanged();
    recompute();
}

void BatchRenamer::setTemplateText(const QString &text)
{
    if (m_templateText == text)
        return;
    m_templateText = text;
    Q_EMIT inputsChanged();
    recompute();
}

void BatchRenamer::setFindText(const QString &text)
{
    if (m_findText == text)
        return;
    m_findText = text;
    Q_EMIT inputsChanged();
    recompute();
}

void BatchRenamer::setReplaceText(const QString &text)
{
    if (m_replaceText == text)
        return;
    m_replaceText = text;
    Q_EMIT inputsChanged();
    recompute();
}

void BatchRenamer::setNumberingOrder(int order)
{
    if (m_numberingOrder == order)
        return;
    m_numberingOrder = order;
    Q_EMIT inputsChanged();
    recompute();
}

QString BatchRenamer::insertTag(const QString &text, int position, const QString &tag) const
{
    const int at = qBound(0, position, int(text.length()));
    return text.left(at) + tag + text.mid(at);
}

QString BatchRenamer::formatted(const Item &item, int number) const
{
    const int extAt = extensionOffset(item.name, item.isDir);
    const QString stem = extAt < 0 ? item.name : item.name.left(extAt);
    const QString extension = extAt < 0 ? QString() : item.name.mid(extAt);

    // Scan for tags; everything between them is literal. The template names
    // the stem only — the extension survives untouched, as in Nautilus.
    QString out;
    int i = 0;
    const QString &t = m_templateText;
    while (i < t.length()) {
        if (t.mid(i).startsWith(kTagOriginal)) {
            out += stem;
            i += kTagOriginal.length();
        } else if (t.mid(i).startsWith(kTagNumber3)) {
            out += QStringLiteral("%1").arg(number, 3, 10, QLatin1Char('0'));
            i += kTagNumber3.length();
        } else if (t.mid(i).startsWith(kTagNumber2)) {
            out += QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0'));
            i += kTagNumber2.length();
        } else if (t.mid(i).startsWith(kTagNumber)) {
            out += QString::number(number);
            i += kTagNumber.length();
        } else {
            out += t.at(i);
            ++i;
        }
    }
    return out + extension;
}

void BatchRenamer::recompute()
{
    m_hasNumbering = m_mode == Template
        && (m_templateText.contains(kTagNumber) || m_templateText.contains(kTagNumber2)
            || m_templateText.contains(kTagNumber3));

    // Numbering follows the chosen order, so the preview shows that order —
    // the number a file gets should be the number the list shows.
    if (m_mode == Template) {
        std::stable_sort(m_items.begin(), m_items.end(), [this](const Item &a, const Item &b) {
            switch (m_numberingOrder) {
            case NameDescending: return m_collator.compare(b.name, a.name) < 0;
            case FirstModified: return a.modified < b.modified;
            case LastModified: return b.modified < a.modified;
            case NameAscending: default: return m_collator.compare(a.name, b.name) < 0;
            }
        });
    }

    int number = 1;
    for (Item &item : m_items) {
        if (m_mode == Template)
            item.newName = formatted(item, number++);
        else if (m_findText.isEmpty())
            item.newName = item.name;
        else
            item.newName = QString(item.name).replace(m_findText, m_replaceText);
        item.conflict = false;
    }

    // A name is in conflict when it collides with another new name, or with a
    // file in the folder that is keeping its name. A name a batch member is
    // renaming *away from* is fine — that is what makes shifts and swaps work,
    // and the worker's two-phase pass makes them safe to execute.
    QHash<QString, int> targetCount;
    for (const Item &item : m_items)
        targetCount[item.newName]++;

    QSet<QString> vacated;
    for (const Item &item : m_items) {
        if (item.newName != item.name)
            vacated.insert(item.name);
    }

    bool anyConflict = false;
    bool anyInvalid = false;
    bool anyChange = false;
    for (Item &item : m_items) {
        if (item.newName != item.name)
            anyChange = true;
        if (!validName(item.newName)) {
            item.conflict = true;
            anyInvalid = true;
            continue;
        }
        if (targetCount.value(item.newName) > 1 && item.newName != item.name) {
            item.conflict = true;
            anyConflict = true;
            continue;
        }
        if (item.newName != item.name && m_existingNames.contains(item.newName)
            && !vacated.contains(item.newName)) {
            item.conflict = true;
            anyConflict = true;
        }
    }

    m_preview.clear();
    for (const Item &item : m_items) {
        QVariantMap row;
        row.insert(QStringLiteral("oldName"), item.name);
        row.insert(QStringLiteral("newName"), item.newName);
        row.insert(QStringLiteral("conflict"), item.conflict);
        m_preview.append(row);
    }

    if (anyInvalid)
        m_problem = tr("File names cannot be empty or contain “/”");
    else if (anyConflict)
        m_problem = tr("File names must be unique");
    else
        m_problem.clear();

    m_canRename = anyChange && !anyConflict && !anyInvalid;
    Q_EMIT previewChanged();
}

QStringList BatchRenamer::sourcePaths() const
{
    QStringList paths;
    for (const Item &item : m_items) {
        if (item.newName != item.name)
            paths << item.path;
    }
    return paths;
}

QStringList BatchRenamer::newNames() const
{
    QStringList names;
    for (const Item &item : m_items) {
        if (item.newName != item.name)
            names << item.newName;
    }
    return names;
}
