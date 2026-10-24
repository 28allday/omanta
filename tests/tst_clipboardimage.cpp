#include "Clipboard.h"

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>

// Pegar una imagen del clipboard (image/png sin ficheros, como "copiar
// imagen" en un navegador) debe crear imagen.png en la carpeta destino.
class TestClipboardImage : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void imagePasteCreatesPngFile();
    void emptyStemFallsBackToEnglish();
};

void TestClipboardImage::imagePasteCreatesPngFile()
{
    Clipboard clipboard;
    QVERIFY(QGuiApplication::clipboard());
    QGuiApplication::clipboard()->clear();
    QVERIFY(!clipboard.hasFiles());

    // Sin el fix: hasImage no existe y esto no compila — ese es el RED.
    QVERIFY(!clipboard.hasImage());

    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    auto *mime = new QMimeData;
    mime->setImageData(image);
    QGuiApplication::clipboard()->setMimeData(mime);

    QVERIFY(clipboard.hasImage());
    QVERIFY(!clipboard.hasFiles());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString first = clipboard.savePastedImage(dir.path(), QStringLiteral("imagen"));
    QVERIFY(!first.isEmpty());
    QVERIFY(first.endsWith(QStringLiteral("imagen.png")));
    QVERIFY(QFileInfo::exists(first));
    QVERIFY(!QImage(first).isNull());

    // Segunda vez: no pisa, crea "imagen (1).png".
    const QString second = clipboard.savePastedImage(dir.path(), QStringLiteral("imagen"));
    QVERIFY(!second.isEmpty());
    QVERIFY(second.endsWith(QStringLiteral("imagen (1).png")));
    QVERIFY(QFileInfo::exists(second));
}

// Sin stem (o solo barras): inglés por defecto, punto.
void TestClipboardImage::emptyStemFallsBackToEnglish()
{
    Clipboard clipboard;
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    auto *mime = new QMimeData;
    mime->setImageData(image);
    QGuiApplication::clipboard()->setMimeData(mime);
    QVERIFY(clipboard.hasImage());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString saved = clipboard.savePastedImage(dir.path(), QString());
    QVERIFY(!saved.isEmpty());
    QVERIFY(saved.endsWith(QStringLiteral("image.png")));
    QVERIFY(QFileInfo::exists(saved));
}

QTEST_MAIN(TestClipboardImage)
#include "tst_clipboardimage.moc"
