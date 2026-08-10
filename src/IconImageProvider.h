#pragma once

#include <QHash>
#include <QMutex>
#include <QPixmap>
#include <QQuickImageProvider>

// Resolves "image://fileicon/<name1>,<name2>,…" two ways:
//
// With a tint ("…?c=rrggbb", appended via Colors.tint() in QML), the GIO
// icon-name candidates map onto omanta' own flat glyph set, rendered in
// exactly that colour — the Omarchy look: monochrome geometry in theme
// colours, not whatever GTK icon theme happens to be installed. The colour
// rides the URL, so a theme switch (or a row becoming selected) re-renders
// automatically through normal property bindings.
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
    QPixmap glyphPixmap(const QString &names, const QString &colorHex, int edge);

    // Rendered glyphs, keyed by glyph|color|edge. The provider is called from
    // QML's image threads, so the cache takes a lock.
    QHash<QString, QPixmap> m_cache;
    QMutex m_mutex;
};
