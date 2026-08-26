#include <QDir>
#include <QRegularExpression>
#include <QTest>

// Every string this app paints is somebody else's data: file names, symlink
// targets, mount labels, error messages from libarchive. Qt's Text and Label
// default to Text.AutoText, which parses whatever looks like markup — a file
// called "<b>invoice.pdf</b>" renders bold with the tags eaten, and a symlink
// target of `<img src="http://host/x.png">` makes the app fetch that URL.
//
// Nothing in the toolchain warns about this: it compiles, it lints, it looks
// right until the day it does not. So the rule is mechanical — every Text and
// Label declares its textFormat — and this test is what keeps it that way.
class TestQmlText : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyTextElementDeclaresItsFormat();
};

void TestQmlText::everyTextElementDeclaresItsFormat()
{
    const QDir qmlDir(QStringLiteral(OMANTA_QML_DIR));
    const QStringList files = qmlDir.entryList({ QStringLiteral("*.qml") }, QDir::Files);
    QVERIFY2(!files.isEmpty(), "no QML found — check OMANTA_QML_DIR");

    // Opens a Text or Label block: the type name at the start of a line, or
    // after "delegate:", "header:", "contentItem:" and friends.
    static const QRegularExpression opener(
        QStringLiteral("(?:^\\s*|[:{]\\s*|component\\s+\\w+:\\s*)(Text|Label)\\s*\\{"));

    QStringList offenders;
    for (const QString &name : files) {
        QFile file(qmlDir.filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines =
            QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));

        for (int i = 0; i < lines.size(); ++i) {
            const QRegularExpressionMatch match = opener.match(lines.at(i));
            if (!match.hasMatch())
                continue;

            // The declaration may sit on the opening line (a one-liner) or in
            // the block that follows, so scan to the end of the block.
            bool declared = lines.at(i).contains(QLatin1String("textFormat"));
            int depth = lines.at(i).count(QLatin1Char('{')) - lines.at(i).count(QLatin1Char('}'));
            for (int j = i + 1; j < lines.size() && depth > 0 && !declared; ++j) {
                declared = lines.at(j).contains(QLatin1String("textFormat"));
                depth += lines.at(j).count(QLatin1Char('{')) - lines.at(j).count(QLatin1Char('}'));
            }
            if (!declared)
                offenders << QStringLiteral("%1:%2").arg(name).arg(i + 1);
        }
    }

    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("Text/Label without an explicit textFormat "
                                       "(use Text.PlainText): ")
                        + offenders.join(QStringLiteral(", "))));
}

QTEST_MAIN(TestQmlText)
#include "tst_qmltext.moc"
