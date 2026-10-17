#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <QVector>

class QQuickItem;
class QQuickWindow;
class QWheelEvent;

// Touchpad scrolling that feels like GTK's (Nautilus), in two parts:
//
//  - Speed. GTK multiplies touchpad (surface-unit) scroll deltas by its
//    MAGIC_SCROLL_FACTOR of 2.5; Qt uses them as they come. Under Omarchy's
//    Hyprland touchpad scroll_factor of 0.4 that left omanta scrolling at 40%
//    of Nautilus's pace. Finger scrolls are re-sent here scaled by 2.5.
//
//  - Glide. Qt Quick's Flickable only glides where the platform sends
//    momentum events (macOS); on Wayland a two-finger scroll stops dead when
//    the fingers lift. At ScrollEnd this carries the Flickable under the
//    pointer on at the release speed, decaying exponentially with GTK's
//    deceleration friction (it travels v/4). Lifting after a pause gives no
//    glide; touching the pad again, clicking or a mouse wheel stops one.
//
// Mouse wheels (events without a scroll phase) pass through untouched.
class KineticScroll : public QObject
{
    Q_OBJECT

public:
    explicit KineticScroll(QObject *parent = nullptr);

    // GTK's values, from gtkscrolledwindow.c.
    static constexpr qreal kTouchpadFactor = 2.5;
    static constexpr qreal kFriction = 4.0;

    // The innermost visible Flickable under a window position that can scroll
    // along one of the given orientations. Public for the test.
    static QQuickItem *flickableAt(QQuickWindow *window, const QPointF &position,
                                   Qt::Orientations orientations);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Sample
    {
        quint64 ms;
        QPointF delta;
    };

    bool handleWheel(QQuickWindow *window, QWheelEvent *event);
    void startGlide(QQuickWindow *window, const QPointF &position, quint64 endMs);
    void stopGlide();
    void step();

    QVector<Sample> m_samples;
    QPointF m_remainder;
    bool m_reposting = false;

    QPointer<QQuickItem> m_target;
    QPointF m_start;
    QPointF m_velocity;
    QPointF m_lastSet;
    QElapsedTimer m_clock;
    QTimer m_frame;
};
