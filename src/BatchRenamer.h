#pragma once

#include <QCollator>
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration>

// The name-generation half of batch rename: turns a selection plus the
// dialog's inputs into a previewable list of new names, with conflicts
// detected before anything touches the disk. Instantiated by the dialog,
// like FileProperties — two windows can be renaming different selections.
//
// The tag strings are Nautilus's exactly ("[1, 2, 3]", "[Original file
// name]"…), because the dialog is a parity item. Metadata tags (Creation
// Date, Track Number…) are a deliberate divergence: they come from tracker,
// which omanta does not depend on.
class BatchRenamer : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY inputsChanged)
    Q_PROPERTY(QString templateText READ templateText WRITE setTemplateText NOTIFY inputsChanged)
    Q_PROPERTY(QString findText READ findText WRITE setFindText NOTIFY inputsChanged)
    Q_PROPERTY(QString replaceText READ replaceText WRITE setReplaceText NOTIFY inputsChanged)
    Q_PROPERTY(int numberingOrder READ numberingOrder WRITE setNumberingOrder NOTIFY inputsChanged)

    Q_PROPERTY(QVariantList preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(bool canRename READ canRename NOTIFY previewChanged)
    Q_PROPERTY(bool hasNumbering READ hasNumbering NOTIFY previewChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY previewChanged)

public:
    enum Mode { Template, FindReplace };
    Q_ENUM(Mode)

    // Nautilus's numbering orders. The default is name-ascending, as there.
    enum NumberingOrder { NameAscending, NameDescending, FirstModified, LastModified };
    Q_ENUM(NumberingOrder)

    explicit BatchRenamer(QObject *parent = nullptr);

    // One selected row: path, display name, mtime, and whether it is a
    // directory (directories get no extension handling). `existingNames` is
    // every name in the folder, hidden included — a hidden file can clash too.
    Q_INVOKABLE void setSelection(const QVariantList &items, const QStringList &existingNames);

    // The changed rows only, parallel lists, in rename order — what
    // FileOperations.batchRename takes.
    Q_INVOKABLE QStringList sourcePaths() const;
    Q_INVOKABLE QStringList newNames() const;

    // Inserts a tag at the cursor position of the template field.
    Q_INVOKABLE QString insertTag(const QString &text, int position, const QString &tag) const;

    int mode() const { return m_mode; }
    void setMode(int mode);
    QString templateText() const { return m_templateText; }
    void setTemplateText(const QString &text);
    QString findText() const { return m_findText; }
    void setFindText(const QString &text);
    QString replaceText() const { return m_replaceText; }
    void setReplaceText(const QString &text);
    int numberingOrder() const { return m_numberingOrder; }
    void setNumberingOrder(int order);

    QVariantList preview() const { return m_preview; }
    bool canRename() const { return m_canRename; }
    bool hasNumbering() const { return m_hasNumbering; }
    QString problem() const { return m_problem; }

    // Where the extension starts in `name`, or -1 for none. Keeps ".bashrc"
    // whole and knows the common compound extensions (".tar.gz"). Public
    // because the tests pin its edge cases directly.
    static int extensionOffset(const QString &name, bool isDir);

Q_SIGNALS:
    void inputsChanged();
    void previewChanged();

private:
    struct Item {
        QString path;
        QString name;
        QDateTime modified;
        bool isDir = false;
        QString newName; // computed
        bool conflict = false;
    };

    void recompute();
    QString formatted(const Item &item, int number) const;

    int m_mode = Template;
    QString m_templateText;
    QString m_findText;
    QString m_replaceText;
    int m_numberingOrder = NameAscending;

    QList<Item> m_items;
    QStringList m_existingNames;
    QCollator m_collator;

    QVariantList m_preview;
    bool m_canRename = false;
    bool m_hasNumbering = false;
    QString m_problem;
};
