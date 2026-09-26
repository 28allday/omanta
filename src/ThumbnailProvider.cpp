#include "ThumbnailProvider.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QQuickImageResponse>
#include <QRunnable>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThreadPool>
#include <QUrl>

#include <gio/gio.h>

namespace {

QMutex g_registryMutex;
bool g_registryLoaded = false;
QHash<QString, QStringList> g_thumbnailers; // mime type → argv template

QString thumbnailRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
           + QStringLiteral("/thumbnails");
}

// The spec keys the cache on the MD5 of the file's URI — not its path — so the
// hash must match byte for byte what other applications produce.
QString hashFor(const QString &filePath)
{
    const QByteArray uri = QUrl::fromLocalFile(filePath).toEncoded();
    return QString::fromLatin1(QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex());
}

QString uriFor(const QString &filePath)
{
    return QString::fromLatin1(QUrl::fromLocalFile(filePath).toEncoded());
}

} // namespace

int ThumbnailCache::bucketFor(int requestedSize)
{
    if (requestedSize <= 128)
        return 128;
    if (requestedSize <= 256)
        return 256;
    if (requestedSize <= 512)
        return 512;
    return 1024;
}

QString ThumbnailCache::bucketName(int bucket)
{
    switch (bucket) {
    case 128: return QStringLiteral("normal");
    case 256: return QStringLiteral("large");
    case 512: return QStringLiteral("x-large");
    default: return QStringLiteral("xx-large");
    }
}

QString ThumbnailCache::cachePathFor(const QString &filePath, int bucket)
{
    return QStringLiteral("%1/%2/%3.png")
        .arg(thumbnailRoot(), bucketName(bucket), hashFor(filePath));
}

QString ThumbnailCache::failMarkerFor(const QString &filePath)
{
    // Kept under our own name so a failure here never suppresses another
    // application's attempt, and vice versa.
    return QStringLiteral("%1/fail/omanta/%2.png").arg(thumbnailRoot(), hashFor(filePath));
}

QImage ThumbnailCache::loadValid(const QString &filePath, int bucket)
{
    const QString cached = cachePathFor(filePath, bucket);
    if (!QFileInfo::exists(cached))
        return {};

    QImage image(cached);
    if (image.isNull())
        return {};

    // Thumb::MTime is what makes the cache correct rather than merely fast: an
    // edited file must not keep showing its old preview.
    const QFileInfo info(filePath);
    const QString recorded = image.text(QStringLiteral("Thumb::MTime"));
    if (recorded.isEmpty() || recorded.toLongLong() != info.lastModified().toSecsSinceEpoch())
        return {};

    // Thumb::MTime has one-second resolution, so a picture re-saved within
    // the second it was written would pass that check. Size (optional in the
    // spec) and our own millisecond stamp catch those, where present.
    const QString size = image.text(QStringLiteral("Thumb::Size"));
    if (!size.isEmpty() && size.toLongLong() != info.size())
        return {};
    const QString msecs = image.text(QStringLiteral("X-Omanta::MTime-MSec"));
    if (!msecs.isEmpty() && msecs.toLongLong() != info.lastModified().toMSecsSinceEpoch())
        return {};

    return image;
}

ThumbnailCache::Version ThumbnailCache::Version::of(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists())
        return {};
    return { info.lastModified().toMSecsSinceEpoch(), info.size() };
}

void ThumbnailCache::store(const QString &filePath, int bucket, QImage image,
                           const Version &rendered)
{
    if (image.isNull() || rendered.size < 0)
        return;

    const QString cached = cachePathFor(filePath, bucket);
    QDir().mkpath(QFileInfo(cached).absolutePath());

    image.setText(QStringLiteral("Thumb::URI"), uriFor(filePath));
    image.setText(QStringLiteral("Thumb::MTime"),
                  QString::number(QDateTime::fromMSecsSinceEpoch(rendered.modifiedMSecs)
                                      .toSecsSinceEpoch()));
    image.setText(QStringLiteral("Thumb::Size"), QString::number(rendered.size));
    image.setText(QStringLiteral("X-Omanta::MTime-MSec"), QString::number(rendered.modifiedMSecs));
    image.setText(QStringLiteral("Software"), QStringLiteral("omanta"));

    image.save(cached, "png");
}

void ThumbnailCache::markFailed(const QString &filePath, const Version &attempted)
{
    if (attempted.size < 0)
        return;
    const QString marker = failMarkerFor(filePath);
    QDir().mkpath(QFileInfo(marker).absolutePath());

    // A 1×1 image carrying the mtime: enough to stop retrying every scroll,
    // and it stops applying once the file itself changes.
    QImage marker1x1(1, 1, QImage::Format_ARGB32);
    marker1x1.fill(Qt::transparent);
    marker1x1.setText(QStringLiteral("Thumb::URI"), uriFor(filePath));
    marker1x1.setText(QStringLiteral("Thumb::MTime"),
                      QString::number(QDateTime::fromMSecsSinceEpoch(attempted.modifiedMSecs)
                                          .toSecsSinceEpoch()));
    marker1x1.setText(QStringLiteral("Thumb::Size"), QString::number(attempted.size));
    marker1x1.save(marker, "png");
}

bool ThumbnailCache::hasFailed(const QString &filePath)
{
    const QString marker = failMarkerFor(filePath);
    if (!QFileInfo::exists(marker))
        return false;

    QImage image(marker);
    const QFileInfo info(filePath);
    const QString recorded = image.text(QStringLiteral("Thumb::MTime"));
    const QString size = image.text(QStringLiteral("Thumb::Size"));
    // A file that has changed since it failed deserves another go.
    return !recorded.isEmpty() && recorded.toLongLong() == info.lastModified().toSecsSinceEpoch()
        && (size.isEmpty() || size.toLongLong() == info.size());
}

void ThumbnailCache::ensureRegistryLoaded()
{
    QMutexLocker lock(&g_registryMutex);
    if (g_registryLoaded)
        return;
    g_registryLoaded = true;

    QStringList directories;
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        directories << base + QStringLiteral("/thumbnailers");

    for (const QString &directory : std::as_const(directories)) {
        const QDir dir(directory);
        if (!dir.exists())
            continue;

        for (const QFileInfo &entry : dir.entryInfoList({ QStringLiteral("*.thumbnailer") },
                                                        QDir::Files)) {
            QFile file(entry.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            // Parsed by hand rather than with QSettings. A .thumbnailer is a
            // desktop-entry file, and QSettings' INI reader mangles them: it
            // treats ';' as a comment introducer, and ';' is exactly the
            // character separating the MimeType list. The result was an empty
            // registry and every video falling back to a generic icon.
            QString exec, tryExec, mimeLine;
            bool inEntry = false;

            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;

                if (line.startsWith(QLatin1Char('['))) {
                    inEntry = line.compare(QLatin1String("[Thumbnailer Entry]"),
                                           Qt::CaseInsensitive) == 0;
                    continue;
                }
                if (!inEntry)
                    continue;

                const int equals = line.indexOf(QLatin1Char('='));
                if (equals <= 0)
                    continue;

                const QString key = line.left(equals).trimmed();
                const QString value = line.mid(equals + 1).trimmed();

                if (key == QLatin1String("Exec"))
                    exec = value;
                else if (key == QLatin1String("TryExec"))
                    tryExec = value;
                else if (key == QLatin1String("MimeType"))
                    mimeLine = value;
            }

            if (exec.isEmpty() || mimeLine.isEmpty())
                continue;
            if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty())
                continue;

            const QStringList argv = QProcess::splitCommand(exec);
            if (argv.isEmpty() || QStandardPaths::findExecutable(argv.first()).isEmpty())
                continue;

            for (const QString &mime : mimeLine.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                if (!g_thumbnailers.contains(mime.trimmed()))
                    g_thumbnailers.insert(mime.trimmed(), argv);
            }
        }
    }
}

bool ThumbnailCache::canHandle(const QString &mimeType)
{
    ensureRegistryLoaded();
    QMutexLocker lock(&g_registryMutex);
    return g_thumbnailers.contains(mimeType);
}

QStringList ThumbnailCache::commandFor(const QString &mimeType)
{
    ensureRegistryLoaded();
    QMutexLocker lock(&g_registryMutex);
    return g_thumbnailers.value(mimeType);
}

// ---------------------------------------------------------------------------

QString ThumbnailCache::contentTypeOf(const QString &filePath)
{
    // g_content_type_guess() with no data looks only at the extension, so a
    // valid PNG saved without one came back as application/octet-stream and
    // never got a thumbnail. query_info sniffs the contents, which is also what
    // the directory model does — so the two agree.
    QString mimeType;
    GFile *file = g_file_new_for_path(filePath.toUtf8().constData());
    if (GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                            G_FILE_QUERY_INFO_NONE, nullptr, nullptr)) {
        if (const char *type = g_file_info_get_content_type(info))
            mimeType = QString::fromUtf8(type);
        g_object_unref(info);
    }
    g_object_unref(file);
    return mimeType;
}

QImage ThumbnailCache::render(const QString &filePath, const QString &mimeType, int size)
{
    QImage image;
    if (mimeType.startsWith(QLatin1String("image/")))
        image = renderImageFile(filePath, size);
    if (image.isNull())
        image = renderViaThumbnailer(filePath, mimeType, size);
    return image;
}

QImage ThumbnailCache::renderImageFile(const QString &filePath, int size)
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true); // honour EXIF orientation

    // Qt caps decoded image allocations at 256MB by default, which rejects
    // ordinary high-resolution scans before they can be scaled down. Generating
    // a thumbnail is precisely the case where decoding a large file is the
    // intended behaviour.
    reader.setAllocationLimit(1024);

    const QSize original = reader.size();
    if (original.isValid()) {
        // Ask the decoder for a reduced size where it can oblige. On a 60MP
        // scan that is the difference between instant and a visible stall.
        QSize target = original;
        target.scale(size, size, Qt::KeepAspectRatio);
        reader.setScaledSize(target);
    }

    QImage image = reader.read();
    if (image.isNull())
        return {};

    if (image.width() > size || image.height() > size)
        image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

QImage ThumbnailCache::renderViaThumbnailer(const QString &filePath, const QString &mimeType, int size)
{
    QStringList argv = ThumbnailCache::commandFor(mimeType);
    if (argv.isEmpty())
        return {};

    QTemporaryFile output(QDir::tempPath() + QStringLiteral("/omanta-thumb-XXXXXX.png"));
    if (!output.open())
        return {};
    output.close();

    const QString program = argv.takeFirst();
    for (QString &argument : argv) {
        argument.replace(QStringLiteral("%i"), filePath);
        argument.replace(QStringLiteral("%u"), QString::fromLatin1(QUrl::fromLocalFile(filePath).toEncoded()));
        argument.replace(QStringLiteral("%o"), output.fileName());
        argument.replace(QStringLiteral("%s"), QString::number(size));
    }

    QProcess process;
    process.start(program, argv);
    // A wedged decoder must not hold a pool thread forever.
    if (!process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};

    return QImage(output.fileName());
}

namespace {

class ThumbnailResponse : public QQuickImageResponse, public QRunnable
{
public:
    ThumbnailResponse(const QString &filePath, const ThumbnailCache::Version &expected, int size)
        : m_filePath(filePath)
        , m_expected(expected)
        , m_bucket(ThumbnailCache::bucketFor(size))
    {
        setAutoDelete(false);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    void run() override
    {
        const QFileInfo info(m_filePath);
        if (!info.exists() || !info.isFile()) {
            fail(QStringLiteral("no such file"));
            return;
        }

        if (superseded(ThumbnailCache::Version::of(m_filePath)))
            return;

        if (QImage cached = ThumbnailCache::loadValid(m_filePath, m_bucket); !cached.isNull()) {
            m_image = cached;
            Q_EMIT finished();
            return;
        }

        if (ThumbnailCache::hasFailed(m_filePath)) {
            fail(QStringLiteral("previously failed"));
            return;
        }

        // A file being saved over while it renders (an editor writing, a
        // copy landing) yields either the old picture or a half-written
        // decode failure. Neither may be recorded against the new version,
        // so render again until the file holds still.
        QImage generated;
        ThumbnailCache::Version version;
        for (int attempt = 0; attempt < 3; ++attempt) {
            version = ThumbnailCache::Version::of(m_filePath);
            const QString mimeType = ThumbnailCache::contentTypeOf(m_filePath);
            generated = ThumbnailCache::render(m_filePath, mimeType, m_bucket);
            if (ThumbnailCache::Version::of(m_filePath) == version) {
                if (generated.isNull())
                    ThumbnailCache::markFailed(m_filePath, version);
                else
                    ThumbnailCache::store(m_filePath, m_bucket, generated, version);
                break;
            }
        }

        if (superseded(version))
            return;

        if (generated.isNull()) {
            fail(QStringLiteral("could not render"));
            return;
        }

        m_image = generated;
        Q_EMIT finished();
    }

    QString errorString() const override { return m_error; }

private:
    // Qt caches the answer under the URL, and the URL names one version of
    // the file. Answering it with a picture of any other version would
    // cache that picture as this version's, to be served back if the file
    // is ever at this version again. The view asks afresh once its model
    // catches up with the file.
    bool superseded(const ThumbnailCache::Version &actual)
    {
        if (m_expected.modifiedMSecs <= 0 || actual == m_expected)
            return false;
        fail(QStringLiteral("file changed"));
        return true;
    }

    void fail(const QString &reason)
    {
        // The view falls back to the file-type icon when a response errors, so
        // failing is a normal outcome here, not an exceptional one.
        m_error = reason;
        Q_EMIT finished();
    }

    QString m_filePath;
    ThumbnailCache::Version m_expected;
    int m_bucket;
    QImage m_image;
    QString m_error;
};

} // namespace

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    const int size = requestedSize.width() > 0 ? requestedSize.width() : 128;
    ThumbnailCache::Version expected;
    const QString path = Thumbnails::pathFromId(id, &expected);
    auto *response = new ThumbnailResponse(path, expected, size);
    QThreadPool::globalInstance()->start(response);
    return response;
}

// ---------------------------------------------------------------------------

Thumbnails::Thumbnails(QObject *parent)
    : QObject(parent)
{
}

void Thumbnails::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    Q_EMIT enabledChanged();
}

void Thumbnails::setMaximumFileSize(qint64 bytes)
{
    if (m_maximumFileSize == bytes)
        return;
    m_maximumFileSize = bytes;
    Q_EMIT maximumFileSizeChanged();
}

bool Thumbnails::canThumbnail(const QString &mimeType, qint64 fileSize) const
{
    if (!m_enabled || mimeType.isEmpty())
        return false;

    // Images are decoded in-process; everything else needs a registered
    // thumbnailer, and asking about a type nothing handles just costs a
    // process launch that will fail.
    //
    // The size cap guards ONLY the in-process image decode — Nautilus's
    // rule. An external thumbnailer (video, PDF) reads a few frames, not
    // the whole file, so a 4GB screen recording still gets its preview.
    if (mimeType.startsWith(QLatin1String("image/")))
        return m_maximumFileSize <= 0 || fileSize <= m_maximumFileSize;

    return ThumbnailCache::canHandle(mimeType);
}

QString Thumbnails::source(const QString &filePath, const QDateTime &modified,
                           qint64 fileSize) const
{
    // "<version>/<encoded path>": the version is opaque to the provider,
    // which only needs the path back.
    const qint64 msecs = modified.isValid() ? modified.toMSecsSinceEpoch() : 0;
    return QStringLiteral("image://thumbnail/%1-%2/%3")
        .arg(msecs)
        .arg(fileSize)
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(filePath, "/")));
}

QString Thumbnails::pathFromId(const QString &id, ThumbnailCache::Version *version)
{
    const int slash = id.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};
    if (version) {
        const QStringList parts = id.left(slash).split(QLatin1Char('-'));
        if (parts.size() == 2)
            *version = { parts.at(0).toLongLong(), parts.at(1).toLongLong() };
    }
    // Qt hands the id over partly decoded, but never a literal '%' (it stays
    // %25), so decoding once more always recovers the original bytes.
    return QUrl::fromPercentEncoding(id.mid(slash + 1).toUtf8());
}
