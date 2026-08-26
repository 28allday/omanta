#include "TestFixture.h"
#include "ThumbnailProvider.h"

#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QTest>
#include <QUrl>

// Thumbnails, tested against the freedesktop spec rather than against my
// assumptions about it. Two of these exist because the first working build got
// them wrong: every video fell back to a generic icon, and a perfectly ordinary
// high-resolution PNG refused to render.
class TestThumbnails : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void bucketsRoundUpToSpecSizes();
    void cachePathFollowsTheSpec();

    void registryFindsVideoThumbnailers();
    void rendersASmallImage();
    void rendersAHighResolutionImage();
    void honoursExifOrientation();

    void storesAndReusesACachedThumbnail();
    void invalidatesTheCacheWhenTheFileChanges();
    void remembersFailuresButNotForever();

    void detectsTypeByContentNotExtension();
    void canThumbnailRespectsTypeAndSize();

private:
    static QString writeImage(const TempTree &tree, const QString &name, int w, int h)
    {
        QImage image(w, h, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QPainter painter(&image);
        painter.fillRect(0, 0, w / 2, h, Qt::magenta);
        painter.end();
        const QString path = tree.filePath(name);
        // Format given explicitly: QImage::save() infers it from the suffix,
        // and this helper is deliberately used to write files without one.
        image.save(path, "png");
        return path;
    }
};

void TestThumbnails::initTestCase()
{
    // Never scribble on the real thumbnail cache while testing.
    QStandardPaths::setTestModeEnabled(true);
}

void TestThumbnails::bucketsRoundUpToSpecSizes()
{
    QCOMPARE(ThumbnailCache::bucketFor(16), 128);
    QCOMPARE(ThumbnailCache::bucketFor(128), 128);
    QCOMPARE(ThumbnailCache::bucketFor(129), 256);
    QCOMPARE(ThumbnailCache::bucketFor(512), 512);
    QCOMPARE(ThumbnailCache::bucketFor(4096), 1024);

    // The directory names are part of the shared-cache contract.
    QCOMPARE(ThumbnailCache::bucketName(128), QStringLiteral("normal"));
    QCOMPARE(ThumbnailCache::bucketName(256), QStringLiteral("large"));
    QCOMPARE(ThumbnailCache::bucketName(512), QStringLiteral("x-large"));
    QCOMPARE(ThumbnailCache::bucketName(1024), QStringLiteral("xx-large"));
}

void TestThumbnails::cachePathFollowsTheSpec()
{
    // The whole point of the spec is a shared cache: the filename must be the
    // MD5 of the file's URI, or thumbnails other applications generated will
    // never be found and ours will never be reused.
    const QString path = QStringLiteral("/home/someone/Pictures/a photo.jpg");
    const QByteArray uri = QUrl::fromLocalFile(path).toEncoded();
    const QString expected =
        QString::fromLatin1(QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex())
        + QStringLiteral(".png");

    const QString cachePath = ThumbnailCache::cachePathFor(path, 256);
    QVERIFY2(cachePath.endsWith(expected), qPrintable(cachePath));
    QVERIFY2(cachePath.contains(QStringLiteral("/thumbnails/large/")), qPrintable(cachePath));
}

void TestThumbnails::registryFindsVideoThumbnailers()
{
    // The first implementation parsed .thumbnailer files with QSettings, whose
    // INI reader treats ';' as a comment introducer — the exact character
    // separating the MimeType list. The registry came back empty and every
    // video silently fell back to a generic icon.
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpegthumbnailer")).isEmpty())
        QSKIP("ffmpegthumbnailer is not installed");

    QVERIFY2(ThumbnailCache::canHandle(QStringLiteral("video/quicktime")),
             "video/quicktime must resolve to a thumbnailer");
    QVERIFY(ThumbnailCache::canHandle(QStringLiteral("video/mp4")));

    const QStringList command = ThumbnailCache::commandFor(QStringLiteral("video/quicktime"));
    QVERIFY(!command.isEmpty());
    QVERIFY(command.first().contains(QStringLiteral("ffmpegthumbnailer")));
    // The placeholders must survive parsing or substitution has nothing to do.
    QVERIFY(command.contains(QStringLiteral("%i")));
    QVERIFY(command.contains(QStringLiteral("%o")));
}

void TestThumbnails::rendersASmallImage()
{
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("small.png"), 400, 300);

    const QImage thumb = ThumbnailCache::render(path, QStringLiteral("image/png"), 256);
    QVERIFY(!thumb.isNull());
    QVERIFY(thumb.width() <= 256 && thumb.height() <= 256);
    // Aspect ratio must survive, or previews look squashed.
    QVERIFY(qAbs(qreal(thumb.width()) / thumb.height() - 4.0 / 3.0) < 0.05);
}

void TestThumbnails::rendersAHighResolutionImage()
{
    // Qt refuses image allocations over 256MB by default, which rejected an
    // ordinary 3160×2272 scan before it could be scaled down. Thumbnailing is
    // exactly the case where decoding a large file is the point.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("scan.png"), 5000, 4000);

    const QImage thumb = ThumbnailCache::render(path, QStringLiteral("image/png"), 256);
    QVERIFY2(!thumb.isNull(), "a high-resolution image must still produce a thumbnail");
    QVERIFY(thumb.width() <= 256 && thumb.height() <= 256);
}

void TestThumbnails::honoursExifOrientation()
{
    // No EXIF here, but the reader must at least not mangle a plain image —
    // the setting that enables rotation is easy to lose in a refactor.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("wide.png"), 600, 200);

    const QImage thumb = ThumbnailCache::renderImageFile(path, 128);
    QVERIFY(!thumb.isNull());
    QVERIFY2(thumb.width() > thumb.height(), "a landscape image must stay landscape");
}

void TestThumbnails::storesAndReusesACachedThumbnail()
{
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("cached.png"), 300, 300);

    const QImage generated = ThumbnailCache::render(path, QStringLiteral("image/png"), 256);
    QVERIFY(!generated.isNull());
    ThumbnailCache::store(path, 256, generated);

    QVERIFY(QFile::exists(ThumbnailCache::cachePathFor(path, 256)));

    const QImage reloaded = ThumbnailCache::loadValid(path, 256);
    QVERIFY2(!reloaded.isNull(), "a freshly stored thumbnail must load back");
    QCOMPARE(reloaded.size(), generated.size());
}

void TestThumbnails::invalidatesTheCacheWhenTheFileChanges()
{
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("edited.png"), 300, 300);
    ThumbnailCache::store(path, 256, ThumbnailCache::render(path, QStringLiteral("image/png"), 256));
    QVERIFY(!ThumbnailCache::loadValid(path, 256).isNull());

    // Edit the file: the stale preview must stop being served.
    QTest::qWait(1100); // mtime has one-second resolution
    writeImage(tree, QStringLiteral("edited.png"), 300, 300);
    tree.setModified(QStringLiteral("edited.png"), QDateTime::currentDateTime().addSecs(5));

    QVERIFY2(ThumbnailCache::loadValid(path, 256).isNull(),
             "an edited file must not keep showing its old thumbnail");
}

void TestThumbnails::remembersFailuresButNotForever()
{
    TempTree tree;
    const QString path = tree.writeFile(QStringLiteral("broken.png"), 32); // not a real PNG

    QVERIFY(!ThumbnailCache::hasFailed(path));
    ThumbnailCache::markFailed(path);
    QVERIFY2(ThumbnailCache::hasFailed(path),
             "a failure must be remembered, or every scroll retries it");

    // A file that has changed deserves another attempt.
    tree.setModified(QStringLiteral("broken.png"), QDateTime::currentDateTime().addSecs(120));
    QVERIFY2(!ThumbnailCache::hasFailed(path),
             "a changed file must be retried rather than written off forever");
}

void TestThumbnails::detectsTypeByContentNotExtension()
{
    // A real PNG saved with no extension. Guessing from the filename returns
    // application/octet-stream, nothing handles that, and the file silently
    // never gets a preview — which is exactly what happened.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("no-extension-here"), 200, 200);

    QCOMPARE(ThumbnailCache::contentTypeOf(path), QStringLiteral("image/png"));

    const QImage thumb = ThumbnailCache::render(path, ThumbnailCache::contentTypeOf(path), 128);
    QVERIFY2(!thumb.isNull(), "an extensionless image must still thumbnail");
}

void TestThumbnails::canThumbnailRespectsTypeAndSize()
{
    Thumbnails thumbnails;

    QVERIFY(thumbnails.canThumbnail(QStringLiteral("image/jpeg"), 1000));
    QVERIFY(!thumbnails.canThumbnail(QStringLiteral("text/plain"), 1000));
    QVERIFY(!thumbnails.canThumbnail(QString(), 1000));

    // The size cap guards the in-process image decode only.
    QVERIFY(!thumbnails.canThumbnail(QStringLiteral("image/jpeg"),
                                     thumbnails.maximumFileSize() + 1));

    // A video over the cap still thumbnails — the external thumbnailer reads
    // frames, not the whole file (Nautilus's rule; a 4GB recording previews).
    // The box's registry has a video thumbnailer — registryFindsVideoThumbnailers
    // pins that separately.
    QVERIFY(thumbnails.canThumbnail(QStringLiteral("video/mp4"),
                                    thumbnails.maximumFileSize() + 1));

    thumbnails.setEnabled(false);
    QVERIFY(!thumbnails.canThumbnail(QStringLiteral("image/jpeg"), 1000));
}

QTEST_MAIN(TestThumbnails)
#include "tst_thumbnails.moc"
