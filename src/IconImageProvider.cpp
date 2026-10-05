#include "IconImageProvider.h"

#include <QIcon>
#include <QPainter>
#include <QSvgRenderer>
#include <QUrlQuery>

namespace {

constexpr int kDefaultSize = 32;

// The glyph set: single-colour flat geometry in a 24x24 box, the same idiom
// as the app icon and omacalc. %C% is replaced with the requested colour.
// Holes (the play triangle, the clock face) are fill-rule="evenodd" subpaths,
// so the theme background shows through rather than a hardcoded second colour.
struct Glyph {
    const char *key;
    const char *svg;
};

const Glyph kGlyphs[] = {
    { "folder",
      R"(<path fill="%C%" d="M3 6.5 A2 2 0 0 1 5 4.5 h4.6 a2 2 0 0 1 1.5.7 l1.2 1.4 a2 2 0 0 0 1.5.7 H19 a2 2 0 0 1 2 2 v8.2 a2 2 0 0 1 -2 2 H5 a2 2 0 0 1 -2 -2 z"/>)" },
    { "file",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6 4 a2 2 0 0 1 2-2 h5 l5 5 v13 a2 2 0 0 1 -2 2 H8 a2 2 0 0 1 -2-2 z M13.2 3.6 v3 a1.2 1.2 0 0 0 1.2 1.2 h3 z"/>)" },
    { "text",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6 4 a2 2 0 0 1 2-2 h5 l5 5 v13 a2 2 0 0 1 -2 2 H8 a2 2 0 0 1 -2-2 z M13.2 3.6 v3 a1.2 1.2 0 0 0 1.2 1.2 h3 z M9 12 h6 v1.5 H9 z M9 15.3 h6 v1.5 H9 z"/>)" },
    { "code",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6 4 a2 2 0 0 1 2-2 h5 l5 5 v13 a2 2 0 0 1 -2 2 H8 a2 2 0 0 1 -2-2 z M13.2 3.6 v3 a1.2 1.2 0 0 0 1.2 1.2 h3 z M10.6 11 9.4 12.1 l1.8 1.9 -1.8 1.9 1.2 1.1 2.9-3 z"/>)" },
    { "image",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 6 a2 2 0 0 1 2-2 h12 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2-2 z M8.4 7.6 a1.9 1.9 0 1 1 0 3.8 a1.9 1.9 0 0 1 0-3.8 z M6.2 18 l3.7-5.3 2.9 3.6 2-2.5 3.3 4.2 z"/>)" },
    { "video",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 6 a2 2 0 0 1 2-2 h12 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2-2 z M10 8.6 l6 3.4 -6 3.4 z"/>)" },
    { "audio",
      R"(<path fill="%C%" d="M17.5 3.1 a1 1 0 0 1 1.2 1 v10.6 a2.7 2.7 0 1 1 -1.8-2.5 V7.7 l-6.2 1.4 v8.3 a2.7 2.7 0 1 1 -1.8-2.5 V6.5 a1 1 0 0 1 .8-1 z"/>)" },
    { "archive",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 5 a1.5 1.5 0 0 1 1.5-1.5 h13 A1.5 1.5 0 0 1 20 5 v2 a1.5 1.5 0 0 1 -1 1.4 V18 a2.5 2.5 0 0 1 -2.5 2.5 h-9 A2.5 2.5 0 0 1 5 18 V8.4 A1.5 1.5 0 0 1 4 7 z M9.5 10.8 h5 v2 h-5 z"/>)" },
    { "terminal",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 6 a2 2 0 0 1 2-2 h12 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2-2 z M7.6 8.8 6.4 10 l2 2 -2 2 1.2 1.2 3.2-3.2 z M12 14.6 h5 v1.6 h-5 z"/>)" },
    { "home",
      R"(<path fill="%C%" d="M12 3.4 3.6 10.6 a1 1 0 0 0 1.3 1.5 l.6-.5 V19 a2 2 0 0 0 2 2 h3.1 v-4.8 a1.4 1.4 0 0 1 2.8 0 V21 h3.1 a2 2 0 0 0 2-2 v-7.4 l.6.5 a1 1 0 0 0 1.3-1.5 z"/>)" },
    { "downloads",
      R"(<path fill="%C%" d="M11.1 3.5 h1.8 v6.9 l2.2-2.2 1.3 1.3 L12 13.9 7.6 9.5 l1.3-1.3 2.2 2.2 z M4 14.5 h3.4 l1.4 2 h6.4 l1.4-2 H20 v3.8 A2.7 2.7 0 0 1 17.3 21 H6.7 A2.7 2.7 0 0 1 4 18.3 z"/>)" },
    { "clock",
      R"(<path fill="%C%" fill-rule="evenodd" d="M12 3 a9 9 0 1 1 0 18 9 9 0 0 1 0-18 z M12 4.9 a7.1 7.1 0 1 0 0 14.2 7.1 7.1 0 0 0 0-14.2 z M11.1 7 h1.8 v5 l3.4 2 -.9 1.5 -4.3-2.5 z"/>)" },
    { "trash-full",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4.9 4.5 a1 1 0 0 0 0 2 h.6 l.9 12.7 A2.5 2.5 0 0 0 8.9 21.5 h6.2 a2.5 2.5 0 0 0 2.5-2.3 L18.5 6.5 h.6 a1 1 0 0 0 0-2 z M9.4 8.8 h1.6 l.2 9.5 h-1.6 z M13 8.8 h1.6 l-.2 9.5 h-1.6 z M10.3 1.3 a1.6 1.6 0 1 1 0 3.2 a1.6 1.6 0 0 1 0-3.2 z M13.7 1.95 a1.25 1.25 0 1 1 0 2.5 a1.25 1.25 0 0 1 0-2.5 z"/>)" },
    { "trash",
      R"(<path fill="%C%" fill-rule="evenodd" d="M9.8 2.5 a1.5 1.5 0 0 0 -1.4 1 l-.4 1 H4.9 a1 1 0 0 0 0 2 h.6 l.9 12.7 A2.5 2.5 0 0 0 8.9 21.5 h6.2 a2.5 2.5 0 0 0 2.5-2.3 L18.5 6.5 h.6 a1 1 0 0 0 0-2 h-3.1 l-.4-1 a1.5 1.5 0 0 0 -1.4-1 z M9.4 8.8 h1.6 l.2 9.5 h-1.6 z M13 8.8 h1.6 l-.2 9.5 h-1.6 z"/>)" },
    { "drive",
      R"(<path fill="%C%" fill-rule="evenodd" d="M6.8 4.5 a1.5 1.5 0 0 1 1.4-1 h7.6 a1.5 1.5 0 0 1 1.4 1 L19.5 11 H4.5 z M4 13 a1.5 1.5 0 0 1 1.5-1.5 h13 A1.5 1.5 0 0 1 20 13 v4 a2.5 2.5 0 0 1 -2.5 2.5 h-11 A2.5 2.5 0 0 1 4 17 z M16 14.8 a1.3 1.3 0 1 1 0 2.6 1.3 1.3 0 0 1 0-2.6 z"/>)" },
    { "network",
      R"(<path stroke="%C%" stroke-width="1.7" fill="none" d="M12 6.4 6.4 17.6 M12 6.4 17.6 17.6 M6.4 17.6 h11.2"/><path fill="%C%" d="M12 3.9 a2.5 2.5 0 1 1 0 5 2.5 2.5 0 0 1 0-5 z M6.4 15.1 a2.5 2.5 0 1 1 0 5 2.5 2.5 0 0 1 0-5 z M17.6 15.1 a2.5 2.5 0 1 1 0 5 2.5 2.5 0 0 1 0-5 z"/>)" },
    { "grid",
      R"(<rect fill="%C%" x="4.6" y="4.6" width="6.5" height="6.5" rx="1.4"/><rect fill="%C%" x="12.9" y="4.6" width="6.5" height="6.5" rx="1.4"/><rect fill="%C%" x="4.6" y="12.9" width="6.5" height="6.5" rx="1.4"/><rect fill="%C%" x="12.9" y="12.9" width="6.5" height="6.5" rx="1.4"/>)" },
    // Toolbar marks, replacing Unicode symbols whose font glyphs snap to
    // the pixel grid unevenly. Drawn on the 16px toolbar grid (1px = 1.5
    // units), so edges land on whole pixels at the size they're shown.
    { "search",
      R"(<circle cx="9.75" cy="9.75" r="6" stroke="%C%" stroke-width="2.25" fill="none"/><path stroke="%C%" stroke-width="3" stroke-linecap="round" d="M14.6 14.6 L20.25 20.25"/>)" },
    { "menu",
      R"(<rect fill="%C%" x="3" y="4.5" width="18" height="3" rx="1.5"/><rect fill="%C%" x="3" y="10.5" width="18" height="3" rx="1.5"/><rect fill="%C%" x="3" y="16.5" width="18" height="3" rx="1.5"/>)" },
    { "list",
      R"(<rect fill="%C%" x="3" y="4.5" width="3" height="3" rx="1"/><rect fill="%C%" x="9" y="4.5" width="12" height="3" rx="1.5"/><rect fill="%C%" x="3" y="10.5" width="3" height="3" rx="1"/><rect fill="%C%" x="9" y="10.5" width="12" height="3" rx="1.5"/><rect fill="%C%" x="3" y="16.5" width="3" height="3" rx="1"/><rect fill="%C%" x="9" y="16.5" width="12" height="3" rx="1.5"/>)" },
    { "back",
      R"(<path stroke="%C%" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round" fill="none" d="M19.5 12 H4.5 M10.5 6 L4.5 12 L10.5 18"/>)" },
    { "forward",
      R"(<path stroke="%C%" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round" fill="none" d="M4.5 12 H19.5 M13.5 6 L19.5 12 L13.5 18"/>)" },
    { "dropdown",
      R"(<path fill="%C%" stroke="%C%" stroke-width="1.5" stroke-linejoin="round" d="M6 9 H18 L12 16.5 z"/>)" },
    { "eject",
      R"(<path fill="%C%" stroke="%C%" stroke-width="1.5" stroke-linejoin="round" d="M12 4.5 L19.5 13.5 H4.5 z"/><rect fill="%C%" x="4.5" y="16.5" width="15" height="3"/>)" },
    { "up",
      R"(<path stroke="%C%" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round" fill="none" d="M12 19.5 V4.5 M6 10.5 L12 4.5 L18 10.5"/>)" },
    { "down",
      R"(<path stroke="%C%" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round" fill="none" d="M12 4.5 V19.5 M6 13.5 L12 19.5 L18 13.5"/>)" },
    { "plus",
      R"(<path stroke="%C%" stroke-width="2.25" stroke-linecap="round" d="M12 4.5 V19.5 M4.5 12 H19.5"/>)" },
    { "minus",
      R"(<path stroke="%C%" stroke-width="2.25" stroke-linecap="round" d="M4.5 12 H19.5"/>)" },
    { "info",
      R"(<path fill="%C%" fill-rule="evenodd" d="M12 3 a9 9 0 1 1 0 18 9 9 0 0 1 0-18 z M12 4.9 a7.1 7.1 0 1 0 0 14.2 7.1 7.1 0 0 0 0-14.2 z"/><rect fill="%C%" x="10.9" y="10.5" width="2.2" height="6"/><circle fill="%C%" cx="12" cy="7.9" r="1.3"/>)" },
    // 12px variants for inline marks (tab close, eject, cancel, the rename
    // arrow): on the 12px grid (1px = 2 units), so straight edges land on
    // whole pixels instead of the 16px set's 2/3-pixel blur.
    { "close-small",
      R"(<path stroke="%C%" stroke-width="3" stroke-linecap="round" d="M6 6 L18 18 M18 6 L6 18"/>)" },
    { "eject-small",
      R"(<path fill="%C%" d="M12 4 L20 14 H4 z"/><rect fill="%C%" x="4" y="16" width="16" height="4"/>)" },
    { "forward-small",
      R"(<rect fill="%C%" x="4" y="12" width="15" height="2"/><path stroke="%C%" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" fill="none" d="M14 7 L20 13 L14 19"/>)" },
    { "close",
      R"(<path stroke="%C%" stroke-width="2.25" stroke-linecap="round" d="M6 6 L18 18 M18 6 L6 18"/>)" },
    { "sidebar",
      R"(<path fill="%C%" fill-rule="evenodd" d="M4 6 a2 2 0 0 1 2-2 h12 a2 2 0 0 1 2 2 v12 a2 2 0 0 1 -2 2 H6 a2 2 0 0 1 -2-2 z M10.5 5.8 v12.4 H18 a.2 .2 0 0 0 .2-.2 V6 a.2 .2 0 0 0 -.2-.2 z"/>)" },
    { "bookmark",
      R"(<path fill="%C%" d="M7 4.7 A2.7 2.7 0 0 1 9.7 2 h4.6 A2.7 2.7 0 0 1 17 4.7 V20.8 a.8 .8 0 0 1 -1.3 .6 L12 18.3 8.3 21.4 A.8 .8 0 0 1 7 20.8 z"/>)" },
    { "star",
      R"(<path fill="%C%" d="M12 2.8 a1 1 0 0 1 .9 .6 l2.3 4.9 5.2 .7 a1 1 0 0 1 .6 1.7 l-3.9 3.7 1 5.3 a1 1 0 0 1 -1.5 1 L12 18.2 l-4.6 2.5 a1 1 0 0 1 -1.5-1 l1-5.3 -3.9-3.7 a1 1 0 0 1 .6-1.7 l5.2-.7 2.3-4.9 a1 1 0 0 1 .9-.6 z"/>)" },
};

const char *glyphSvg(const QString &key)
{
    for (const Glyph &glyph : kGlyphs) {
        if (key == QLatin1String(glyph.key))
            return glyph.svg;
    }
    return nullptr;
}

// File views keep the folder silhouette even for special locations. Use
// GIO's identities (including user-dir configuration), never display names.
QString contentFolder(QString name)
{
    if (name.endsWith(QLatin1String("-symbolic")))
        name.chop(9);
    if (name == QLatin1String("user-home"))
        return QStringLiteral("content-folder-home");
    for (const char *kind : {"documents", "download", "pictures", "music", "videos"}) {
        if (name == QLatin1String("folder-") + QLatin1String(kind))
            return QStringLiteral("content-folder-") + QLatin1String(kind);
    }
    if (name.startsWith(QLatin1String("folder")) || name == QLatin1String("inode-directory"))
        return QStringLiteral("content-folder");
    return {};
}

QString folderSvg(const QString &glyph, bool details)
{
    // The back tab sits behind a rounded front panel. Two solid tones stay
    // legible at small sizes, without shadows or a fixed theme background.
    QString svg = QStringLiteral(
        "<path fill=\"%B%\" d=\"M2 6.5 A2 2 0 0 1 4 4.5 H9 "
        "a2 2 0 0 1 1.5.7 l1.3 1.6 H20 a2 2 0 0 1 2 2 V18 "
        "a2 2 0 0 1 -2 2 H4 a2 2 0 0 1 -2 -2 z\"/>"
        "<rect fill=\"%C%\" x=\"2\" y=\"9\" width=\"20\" height=\"11\" rx=\"2\"/>");

    // At 16/18px an emblem competes with the silhouette. Reveal the detail
    // once there is enough room to render its strokes cleanly.
    if (!details)
        return svg;

    QString emblem;
    if (glyph == QLatin1String("content-folder-documents"))
        emblem = QStringLiteral("<rect x=\"9\" y=\"11\" width=\"6\" height=\"7\" rx=\".7\"/>"
                                "<path d=\"M10.7 13.4 h2.6 M10.7 15.6 h2.6\"/>");
    else if (glyph == QLatin1String("content-folder-download"))
        emblem = QStringLiteral("<path d=\"M12 11 v5 M9.5 13.5 12 16 l2.5-2.5 "
                                "M9 17.8 h6\"/>");
    else if (glyph == QLatin1String("content-folder-pictures"))
        emblem = QStringLiteral("<rect x=\"8\" y=\"11.5\" width=\"8\" height=\"6\" rx=\".7\"/>"
                                "<circle cx=\"10.3\" cy=\"13.4\" r=\".65\" fill=\"%E%\" stroke=\"none\"/>"
                                "<path d=\"M8.5 17 11 14.5 l1.6 1.5 1.4-1 1.5 2\"/>");
    else if (glyph == QLatin1String("content-folder-music"))
        emblem = QStringLiteral("<path d=\"M11 16.6 v-5 l4-1 v5\"/>"
                                "<g fill=\"%E%\" stroke=\"none\">"
                                "<ellipse cx=\"9.8\" cy=\"16.8\" rx=\"1.7\" ry=\"1.2\"/>"
                                "<ellipse cx=\"13.8\" cy=\"15.8\" rx=\"1.7\" ry=\"1.2\"/></g>");
    else if (glyph == QLatin1String("content-folder-videos"))
        emblem = QStringLiteral("<path fill=\"%E%\" stroke=\"none\" d=\"M10 11.3 15.5 14.5 10 17.7 z\"/>");
    else if (glyph == QLatin1String("content-folder-home"))
        emblem = QStringLiteral("<path d=\"M8.3 14 12 10.8 15.7 14 "
                                "M9.3 13.4 v4.4 h5.4 v-4.4 M12 17.8 v-2.5\"/>");
    if (!emblem.isEmpty())
        svg += QStringLiteral("<g fill=\"none\" stroke=\"%E%\" stroke-width=\"1.15\" "
                              "stroke-linecap=\"round\" stroke-linejoin=\"round\">%1</g>").arg(emblem);
    return svg;
}

// One GIO icon-name candidate → a glyph key, or empty for "no opinion".
QString glyphForName(QString name)
{
    if (name.endsWith(QLatin1String("-symbolic")))
        name.chop(9);

    // The app's own chrome (not a GIO name): the view-switch button.
    if (name == QLatin1String("view-grid")) return QStringLiteral("grid");
    if (name == QLatin1String("view-sidebar")) return QStringLiteral("sidebar");
    if (name == QLatin1String("view-list")) return QStringLiteral("list");
    for (const char *mark : {"search", "menu", "back", "forward", "dropdown", "close", "eject",
                             "up", "down", "plus", "minus", "info",
                             "close-small", "eject-small", "forward-small"}) {
        if (name == QLatin1String("toolbar-") + QLatin1String(mark))
            return QLatin1String(mark);
    }

    // The sidebar's specials and everything folder-ish.
    if (name == QLatin1String("user-home")) return QStringLiteral("home");
    if (name == QLatin1String("folder-documents")) return QStringLiteral("text");
    if (name == QLatin1String("folder-download")) return QStringLiteral("downloads");
    if (name == QLatin1String("folder-music")) return QStringLiteral("audio");
    if (name == QLatin1String("folder-pictures")) return QStringLiteral("image");
    if (name == QLatin1String("folder-videos")) return QStringLiteral("video");
    if (name == QLatin1String("document-open-recent")) return QStringLiteral("clock");
    if (name == QLatin1String("user-bookmarks")) return QStringLiteral("bookmark");
    if (name.startsWith(QLatin1String("starred")) || name.startsWith(QLatin1String("star")))
        return QStringLiteral("star");
    // Order matters: "user-trash-full" also startsWith "user-trash".
    if (name.startsWith(QLatin1String("user-trash-full"))) return QStringLiteral("trash-full");
    if (name.startsWith(QLatin1String("user-trash"))) return QStringLiteral("trash");
    if (name.startsWith(QLatin1String("network-"))) return QStringLiteral("network");
    if (name.startsWith(QLatin1String("folder")) || name == QLatin1String("inode-directory"))
        return QStringLiteral("folder");

    if (name.startsWith(QLatin1String("drive-")) || name.startsWith(QLatin1String("media-"))
        || name.startsWith(QLatin1String("phone")) || name.startsWith(QLatin1String("camera")))
        return QStringLiteral("drive");

    if (name.startsWith(QLatin1String("image-"))) return QStringLiteral("image");
    if (name.startsWith(QLatin1String("video-"))) return QStringLiteral("video");
    if (name.startsWith(QLatin1String("audio-"))) return QStringLiteral("audio");
    if (name.startsWith(QLatin1String("font-"))) return QStringLiteral("text");
    if (name == QLatin1String("application-x-executable")) return QStringLiteral("terminal");
    if (name == QLatin1String("application-pdf")) return QStringLiteral("text");

    if (name.startsWith(QLatin1String("package-")) || name.contains(QLatin1String("archive"))
        || name.contains(QLatin1String("compressed")) || name.contains(QLatin1String("-zip")))
        return QStringLiteral("archive");

    if (name.startsWith(QLatin1String("text-x-")) || name.contains(QLatin1String("script")))
        return name == QLatin1String("text-x-generic") ? QStringLiteral("text")
                                                       : QStringLiteral("code");
    if (name.startsWith(QLatin1String("text-"))) return QStringLiteral("text");

    return {};
}

} // namespace

IconImageProvider::IconImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap IconImageProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int edge = requestedSize.width() > 0 ? requestedSize.width() : kDefaultSize;

    const qsizetype queryStart = id.indexOf(QLatin1Char('?'));
    const QUrlQuery query(queryStart < 0 ? QString() : id.mid(queryStart + 1));
    QPixmap pixmap = !query.hasQueryItem(QStringLiteral("c"))
        ? themedPixmap(id, edge)
        : glyphPixmap(id.left(queryStart), query.queryItemValue(QStringLiteral("c")), edge,
                      query.queryItemValue(QStringLiteral("style")) == QLatin1String("content"),
                      edge >= 24 && query.queryItemValue(QStringLiteral("detail")) != QLatin1String("simple"));

    if (size)
        *size = pixmap.size();
    return pixmap;
}

QPixmap IconImageProvider::themedPixmap(const QString &names, int edge) const
{
    QIcon icon;
    const QStringList candidates = names.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &name : candidates) {
        if (QIcon::hasThemeIcon(name)) {
            icon = QIcon::fromTheme(name);
            break;
        }
    }

    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));

    QPixmap pixmap = icon.pixmap(QSize(edge, edge));

    if (pixmap.isNull()) {
        // A themeless system would otherwise render a grid of broken-image
        // glyphs. A blank square of the right size is a better failure.
        pixmap = QPixmap(edge, edge);
        pixmap.fill(Qt::transparent);
    }
    return pixmap;
}

QPixmap IconImageProvider::glyphPixmap(const QString &names, const QString &colorHex, int edge,
                                      bool content, bool details)
{
    QString glyph;
    const QStringList candidates = names.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &name : candidates) {
        glyph = content ? contentFolder(name) : QString();
        if (glyph.isEmpty())
            glyph = glyphForName(name);
        if (!glyph.isEmpty())
            break;
    }
    if (glyph.isEmpty())
        glyph = QStringLiteral("file");

    const bool folder = glyph.startsWith(QLatin1String("content-folder"));
    const QString cacheKey = glyph + QLatin1Char('|') + colorHex + QLatin1Char('|')
                             + QString::number(edge)
                             + (folder && details ? QLatin1String("|full") : QLatin1String("|simple"));
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_cache.constFind(cacheKey);
        if (it != m_cache.constEnd())
            return it.value();
    }

    QColor color(QLatin1Char('#') + colorHex);
    if (!color.isValid())
        color = Qt::gray;

    // SVG has no 8-digit hex; the colour goes in as #rrggbb and any alpha
    // (a QML colour stringifies as #aarrggbb) rides on painter opacity.
    QString svg = QStringLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">%1</svg>")
        .arg(folder ? folderSvg(glyph, details) : QString::fromLatin1(glyphSvg(glyph)));
    svg.replace(QLatin1String("%C%"), color.name(QColor::HexRgb));
    if (folder) {
        svg.replace(QLatin1String("%B%"), color.darker(135).name(QColor::HexRgb));
        // Contrast against the front panel while retaining a little of its
        // hue. This also keeps emblems visible on white/black selected icons.
        const qreal luminance = .2126 * color.redF() + .7152 * color.greenF() + .0722 * color.blueF();
        const qreal target = luminance > .5 ? 0 : 1;
        const QColor emblem = QColor::fromRgbF(.4 * color.redF() + .6 * target,
                                               .4 * color.greenF() + .6 * target,
                                               .4 * color.blueF() + .6 * target);
        svg.replace(QLatin1String("%E%"), emblem.name(QColor::HexRgb));
    }

    QImage image(edge, edge, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg.toUtf8());
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(color.alphaF());
    renderer.render(&painter);
    painter.end();

    QPixmap pixmap = QPixmap::fromImage(image);
    QMutexLocker locker(&m_mutex);
    m_cache.insert(cacheKey, pixmap);
    return pixmap;
}
