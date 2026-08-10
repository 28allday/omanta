#include "Clipboard.h"
#include "Location.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

namespace {

constexpr const char *kGnomeFormat = "x-special/gnome-copied-files";

} // namespace

Clipboard::Clipboard(QObject *parent)
    : QObject(parent)
{
    if (auto *clipboard = QGuiApplication::clipboard())
        connect(clipboard, &QClipboard::dataChanged, this, &Clipboard::changed);
}

void Clipboard::put(const QStringList &paths, bool cut)
{
    auto *clipboard = QGuiApplication::clipboard();
    if (!clipboard || paths.isEmpty())
        return;

    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &path : paths) {
        urls.append(Location::isUri(path) ? QUrl(path) : QUrl::fromLocalFile(path));
    }

    // The GNOME format is a plain-text payload: an operation word, then one URI
    // per line. Getting the leading word wrong turns a copy into a move.
    QByteArray payload = cut ? QByteArray("cut") : QByteArray("copy");
    for (const QUrl &url : urls)
        payload += '\n' + url.toEncoded();

    auto *mime = new QMimeData;
    mime->setData(QLatin1String(kGnomeFormat), payload);
    mime->setUrls(urls);

    clipboard->setMimeData(mime);
    Q_EMIT changed();
}

void Clipboard::copyFiles(const QStringList &paths)
{
    put(paths, false);
}

void Clipboard::cutFiles(const QStringList &paths)
{
    put(paths, true);
}

QStringList Clipboard::paths() const
{
    auto *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData *mime = clipboard->mimeData();
    if (!mime)
        return {};

    QStringList result;

    if (mime->hasFormat(QLatin1String(kGnomeFormat))) {
        const QList<QByteArray> lines = mime->data(QLatin1String(kGnomeFormat)).split('\n');
        for (int i = 1; i < lines.size(); ++i) { // line 0 is "copy" or "cut"
            const QUrl url = QUrl::fromEncoded(lines.at(i).trimmed());
            if (url.isValid() && !url.scheme().isEmpty())
                result.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
        }
        return result;
    }

    // Anything that offers URIs — a browser download, another file manager —
    // is still worth accepting.
    for (const QUrl &url : mime->urls()) {
        if (url.isValid() && !url.scheme().isEmpty())
            result.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
    }
    return result;
}

bool Clipboard::isCut() const
{
    auto *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;

    const QMimeData *mime = clipboard->mimeData();
    if (!mime || !mime->hasFormat(QLatin1String(kGnomeFormat)))
        return false;

    return mime->data(QLatin1String(kGnomeFormat)).startsWith("cut");
}

void Clipboard::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void Clipboard::clear()
{
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->clear();
    Q_EMIT changed();
}
