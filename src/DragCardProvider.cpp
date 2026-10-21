#include "DragCardProvider.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QUrlQuery>
#include <QtMath>

#include "ThumbnailProvider.h"

namespace {

// The card's geometry, in logical pixels: the same measures the QML card had.
constexpr int kHeight = 56;
constexpr int kMinWidth = 120;
constexpr int kMaxWidth = 300;
constexpr int kPicture = 36;
constexpr int kPictureInset = 10;
constexpr int kLabelX = 56;
constexpr int kLabelRightMargin = 12;
constexpr int kRadius = 6;

const QLatin1String kIconScheme("image://fileicon/");
const QLatin1String kThumbnailScheme("image://thumbnail/");

QFont labelFont()
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(13);
    return font;
}

QColor colorFrom(const QUrlQuery &query, const QString &key, const QColor &fallback)
{
    const QColor color(query.queryItemValue(key, QUrl::FullyDecoded));
    return color.isValid() ? color : fallback;
}

} // namespace

DragCardProvider::DragCardProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QSize DragCardProvider::cardSize(const QString &label)
{
    const qreal text = QFontMetricsF(labelFont()).horizontalAdvance(label);
    return { qBound(kMinWidth, qCeil(text) + 70, kMaxWidth), kHeight };
}

QPixmap DragCardProvider::requestPixmap(const QString &id, QSize *size, const QSize &)
{
    const qsizetype queryStart = id.indexOf(QLatin1Char('?'));
    const QUrlQuery query(queryStart < 0 ? QString() : id.mid(queryStart + 1));
    const QString label = query.queryItemValue(QStringLiteral("label"), QUrl::FullyDecoded);
    const qreal dpr = qBound(1.0, query.queryItemValue(QStringLiteral("dpr")).toDouble(), 4.0);

    const QSize logical = cardSize(label);
    QImage card((QSizeF(logical) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
    card.setDevicePixelRatio(dpr);
    card.fill(Qt::transparent);

    QPainter painter(&card);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // Fill, then a one-pixel border inside the edge, as a QML Rectangle draws.
    const QRectF bounds(QPointF(0, 0), QSizeF(logical));
    painter.setPen(Qt::NoPen);
    painter.setBrush(colorFrom(query, QStringLiteral("bg"), Qt::black));
    painter.drawRoundedRect(bounds, kRadius, kRadius);
    painter.setPen(QPen(colorFrom(query, QStringLiteral("border"), Qt::darkGray), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), kRadius - 0.5, kRadius - 0.5);

    // The thumbnail or icon, fitted and centred in its square.
    const int edge = qRound(kPicture * dpr);
    const QRectF square(kPictureInset, kPictureInset, kPicture, kPicture);
    QImage image = picture(query.queryItemValue(QStringLiteral("picture"), QUrl::FullyDecoded), edge);
    if (image.isNull())
        image = picture(query.queryItemValue(QStringLiteral("icon"), QUrl::FullyDecoded), edge);
    if (!image.isNull()) {
        image = image.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRectF target(QPointF(0, 0), QSizeF(image.size()) / dpr);
        target.moveCenter(square.center());
        painter.drawImage(target, image);
    }

    const QRectF labelBox(kLabelX, 0, logical.width() - kLabelX - kLabelRightMargin, kHeight);
    const QFont font = labelFont();
    painter.setFont(font);
    painter.setPen(colorFrom(query, QStringLiteral("fg"), Qt::white));
    painter.drawText(labelBox, Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetricsF(font).elidedText(label, Qt::ElideMiddle, labelBox.width()));
    painter.end();

    if (size)
        *size = card.size();
    return QPixmap::fromImage(std::move(card));
}

QImage DragCardProvider::picture(const QString &source, int edge)
{
    if (source.startsWith(kIconScheme))
        return m_icons.requestPixmap(source.mid(kIconScheme.size()), nullptr, QSize(edge, edge)).toImage();
    if (!source.startsWith(kThumbnailScheme))
        return {};
    const QString path = Thumbnails::pathFromId(source.mid(kThumbnailScheme.size()));
    if (path.isEmpty())
        return {};
    // The view may have made it at any zoom; the smallest current one that
    // covers the card's square will do.
    for (int bucket = ThumbnailCache::bucketFor(edge); ; bucket *= 2) {
        if (QImage image = ThumbnailCache::loadValid(path, bucket); !image.isNull())
            return image;
        if (bucket >= 1024)
            return {};
    }
}
