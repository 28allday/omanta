#pragma once

#include <QHash>
#include <QMutex>
#include <QPixmap>
#include <QQuickImageProvider>

// Resolves "image://fileicon/<name1>,<name2>,…" two ways:
//
// With a tint ("…?c=rrggbb", appended via Colors.tint() in QML), the GIO
// icon-name candidates map onto omanta's own glyph set. The sidebar and
// chrome use monochrome symbols; "&style=content" adds two-tone folders
// and special-folder emblems in file views. All colours derive from the
// supplied theme/selection colour. The URL carries the style and colour,
// so theme switches and selection changes re-render through QML bindings.
//
// Without a tint, the candidates resolve against the icon theme as before.
// That path stays for the icons that are genuinely someone else's brand —
// the applications in the Open With list.
class IconImageProvider : public QQuickImageProvider
{
public:
    IconImageProvider();

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QPixmap themedPixmap(const QString &names, int edge) const;
    QPixmap glyphPixmap(const QString &names, const QString &colorHex, int edge,
                        bool content, bool details);

    // Rendered glyphs, keyed by glyph|color|edge|detail. Content folders have their
    // own glyph keys, keeping them separate from sidebar icons at any size.
    // The provider is called from QML's image threads, so the cache takes a lock.
    QHash<QString, QPixmap> m_cache;
    QMutex m_mutex;
};
