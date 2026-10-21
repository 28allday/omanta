#pragma once

#include <QPixmap>
#include <QQuickImageProvider>

#include "IconImageProvider.h"

// Paints the card a file drag carries under the pointer:
// "image://dragcard/card?label=…&picture=…&icon=…&bg=…&fg=…&border=…&dpr=…",
// every value percent-encoded (FileDrag.qml builds it).
//
// The card used to be a QML item captured with grabToImage. A grab runs its
// last step on the scene graph's render thread and unhooks itself from the
// window's signals there, while the item it belongs to lives in a view
// delegate that the GUI thread may tear down at any moment (a click that
// opens a folder resets the model). Painting with QPainter keeps the render
// thread out of it: a Pixmap provider is only ever called synchronously on the
// GUI thread, which is where Drag loads its imageSource.
//
// The picture is what the view shows for the file: its icon (selection colour
// and all), or its thumbnail when the shared thumbnail cache holds a current
// one. Otherwise it is the plain icon. Nothing is generated here: a drag
// starts at once and never waits on a decoder.
class DragCardProvider : public QQuickImageProvider
{
public:
    DragCardProvider();

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

    // The card's logical size for a label, as the painter lays it out.
    static QSize cardSize(const QString &label);

private:
    // An image://fileicon or image://thumbnail source, drawn edge pixels square.
    QImage picture(const QString &source, int edge);

    IconImageProvider m_icons;
};
