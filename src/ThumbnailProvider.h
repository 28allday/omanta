#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QStringList>
#include <QtQmlIntegration>

// Thumbnails, implemented against the freedesktop thumbnail spec rather than a
// GNOME library.
//
// The payoff for following the spec is a *shared* cache: thumbnails other
// applications already generated show up instantly here, and the ones generated
// here show up in them. On this system that means ffmpegthumbnailer, evince and
// glycin do the work for video, PDF and HEIF/JXL/SVG, and omanta only has to
// find and cache the results.
//
// Generation runs on a thread pool. The icon provider deliberately does not —
// icons are cheap and immediate — but a thumbnail can mean spawning a process
// to decode a video, and doing that on the GUI thread would stutter the window
// on every scroll.
class ThumbnailCache
{
public:
    // The spec's size buckets. Requests are rounded up to one of these so the
    // cache is shared rather than fragmented per-widget-size.
    static int bucketFor(int requestedSize);
    static QString bucketName(int bucket);

    // Where the spec says this file's thumbnail lives, whoever made it.
    static QString cachePathFor(const QString &filePath, int bucket);
    static QString failMarkerFor(const QString &filePath);

    // A cached thumbnail is only valid while the source hasn't changed since.
    static QImage loadValid(const QString &filePath, int bucket);
    static void store(const QString &filePath, int bucket, QImage image);
    static void markFailed(const QString &filePath);
    static bool hasFailed(const QString &filePath);

    // Registered .thumbnailer handlers, keyed by MIME type. Parsed once.
    static bool canHandle(const QString &mimeType);
    static QStringList commandFor(const QString &mimeType);

    // Exposed so the tests can drive rendering directly rather than through a
    // QML image provider.
    // Content type by sniffing, not by extension — a valid image saved
    // without one must still get a preview.
    static QString contentTypeOf(const QString &filePath);

    static QImage render(const QString &filePath, const QString &mimeType, int size);
    static QImage renderImageFile(const QString &filePath, int size);
    static QImage renderViaThumbnailer(const QString &filePath, const QString &mimeType, int size);

private:
    static void ensureRegistryLoaded();
};

class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

// What QML needs to decide whether to even ask for a thumbnail.
class Thumbnails : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(qint64 maximumFileSize READ maximumFileSize WRITE setMaximumFileSize
                   NOTIFY maximumFileSizeChanged)

public:
    explicit Thumbnails(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    qint64 maximumFileSize() const { return m_maximumFileSize; }
    void setMaximumFileSize(qint64 bytes);

    // True when this file is worth asking about at all: the type is supported
    // and the file is not so large that decoding it would cost more than the
    // preview is worth.
    Q_INVOKABLE bool canThumbnail(const QString &mimeType, qint64 fileSize) const;

Q_SIGNALS:
    void enabledChanged();
    void maximumFileSizeChanged();

private:
    bool m_enabled = true;
    // Nautilus defaults to 50MB; the same number keeps behaviour familiar.
    qint64 m_maximumFileSize = 50LL * 1024 * 1024;
};
