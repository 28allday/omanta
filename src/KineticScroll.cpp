#include "KineticScroll.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>

#include <cmath>
#include <functional>

namespace {

// Only the last stretch of the swipe sets the speed, like a finger flick.
constexpr quint64 kVelocityWindowMs = 100;
// Fingers held still this long before lifting means "stop here", no glide.
constexpr quint64 kPauseBeforeLiftMs = 60;
// Slower than this (px/s) is a placement, not a flick.
constexpr qreal kMinimumVelocity = 150;
// The glide ends once it has slowed below this (px/s).
constexpr qreal kStopVelocity = 10;

bool canScroll(QQuickItem *item, Qt::Orientations orientations)
{
    return ((orientations & Qt::Vertical)
            && item->property("contentHeight").toReal() > item->height())
        || ((orientations & Qt::Horizontal)
            && item->property("contentWidth").toReal() > item->width());
}

// Flickable's scroll range along one axis, margins included.
void range(QQuickItem *item, bool vertical, qreal &min, qreal &max)
{
    const char *origin = vertical ? "originY" : "originX";
    const char *before = vertical ? "topMargin" : "leftMargin";
    const char *after = vertical ? "bottomMargin" : "rightMargin";
    const char *content = vertical ? "contentHeight" : "contentWidth";
    const qreal extent = vertical ? item->height() : item->width();
    min = item->property(origin).toReal() - item->property(before).toReal();
    max = qMax(min, item->property(origin).toReal() + item->property(content).toReal()
                        + item->property(after).toReal() - extent);
}

} // namespace

KineticScroll::KineticScroll(QObject *parent)
    : QObject(parent)
{
    m_frame.setTimerType(Qt::PreciseTimer);
    m_frame.setInterval(8);
    connect(&m_frame, &QTimer::timeout, this, &KineticScroll::step);
}

QQuickItem *KineticScroll::flickableAt(QQuickWindow *window, const QPointF &position,
                                       Qt::Orientations orientations)
{
    if (!window || !window->contentItem())
        return nullptr;

    // Every Flickable whose area holds the point, keeping the deepest: the
    // views put overlays beside themselves, so the topmost item under the
    // pointer is often not inside the Flickable at all.
    QQuickItem *best = nullptr;
    int bestDepth = -1;
    std::function<void(QQuickItem *, int)> walk = [&](QQuickItem *item, int depth) {
        if (!item->isVisible())
            return;
        if (item->inherits("QQuickFlickable") && canScroll(item, orientations)
            && item->contains(item->mapFromScene(position)) && depth >= bestDepth) {
            best = item;
            bestDepth = depth;
        }
        for (QQuickItem *child : item->childItems())
            walk(child, depth + 1);
    };
    walk(window->contentItem(), 0);
    return best;
}

bool KineticScroll::eventFilter(QObject *watched, QEvent *event)
{
    // Qt Quick re-sends events on to items; the window sees each one once.
    auto *window = qobject_cast<QQuickWindow *>(watched);
    if (!window || m_reposting)
        return QObject::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Wheel:
        if (handleWheel(window, static_cast<QWheelEvent *>(event)))
            return true;
        break;
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
        stopGlide();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

bool KineticScroll::handleWheel(QQuickWindow *window, QWheelEvent *event)
{
    // A mouse wheel (no phase) or a "dumb" touchpad: stop any glide and leave
    // the event alone. Wayland marks finger scrolls as system-synthesized.
    if (event->phase() == Qt::NoScrollPhase || event->phase() == Qt::ScrollMomentum
        || event->source() != Qt::MouseEventSynthesizedBySystem) {
        stopGlide();
        return false;
    }

    switch (event->phase()) {
    case Qt::ScrollBegin:
        // Fingers back on the pad: catch the page, as GTK does.
        stopGlide();
        m_samples.clear();
        m_remainder = {};
        return false;
    case Qt::ScrollEnd:
        startGlide(window, event->position(), event->timestamp());
        m_samples.clear();
        return false;
    default:
        break;
    }

    if (event->pixelDelta().isNull())
        return false;

    // Scale as GTK does, carrying the rounding so slow scrolls keep pace.
    const QPointF scaled = QPointF(event->pixelDelta()) * kTouchpadFactor + m_remainder;
    const QPoint delta(qRound(scaled.x()), qRound(scaled.y()));
    m_remainder = scaled - QPointF(delta);
    m_samples.append({event->timestamp(), QPointF(delta)});
    if (delta.isNull())
        return true;

    QWheelEvent faster(event->position(), event->globalPosition(), delta, event->angleDelta(),
                       event->buttons(), event->modifiers(), event->phase(), event->inverted(),
                       event->source(), event->pointingDevice());
    faster.setTimestamp(event->timestamp());
    m_reposting = true;
    QCoreApplication::sendEvent(window, &faster);
    m_reposting = false;
    return true;
}

void KineticScroll::startGlide(QQuickWindow *window, const QPointF &position, quint64 endMs)
{
    if (m_samples.size() < 2 || endMs - m_samples.last().ms > kPauseBeforeLiftMs)
        return;

    const quint64 from = m_samples.last().ms > kVelocityWindowMs
        ? m_samples.last().ms - kVelocityWindowMs : 0;
    int first = m_samples.size() - 1;
    while (first > 0 && m_samples.at(first - 1).ms >= from)
        --first;
    if (first == m_samples.size() - 1)
        --first;

    // Each delta is the movement since the sample before it, so the first
    // sample only marks the start time.
    QPointF distance;
    for (int i = first + 1; i < m_samples.size(); ++i)
        distance += m_samples.at(i).delta;
    const qreal seconds = qMax<quint64>(m_samples.last().ms - m_samples.at(first).ms, 8) / 1000.0;
    // A positive delta scrolls back towards the start (content moves down).
    const QPointF velocity = -distance / seconds;
    if (std::hypot(velocity.x(), velocity.y()) < kMinimumVelocity)
        return;

    Qt::Orientations orientations;
    if (velocity.y() != 0)
        orientations |= Qt::Vertical;
    if (velocity.x() != 0)
        orientations |= Qt::Horizontal;
    QQuickItem *flickable = flickableAt(window, position, orientations);
    if (!flickable)
        return;

    m_target = flickable;
    m_start = QPointF(flickable->property("contentX").toReal(),
                      flickable->property("contentY").toReal());
    m_lastSet = m_start;
    m_velocity = velocity;
    m_clock.start();
    m_frame.start();
}

void KineticScroll::stopGlide()
{
    m_frame.stop();
    m_target = nullptr;
}

void KineticScroll::step()
{
    if (!m_target) {
        stopGlide();
        return;
    }
    const QPointF current(m_target->property("contentX").toReal(),
                          m_target->property("contentY").toReal());
    // Something else moved the view (a new folder, a keypress): let it be.
    if (std::abs(current.x() - m_lastSet.x()) > 1 || std::abs(current.y() - m_lastSet.y()) > 1) {
        stopGlide();
        return;
    }

    // GTK's deceleration: x(t) = x0 + v/f * (1 - e^(-f t)), v(t) = v e^(-f t).
    const qreal t = m_clock.nsecsElapsed() / 1e9;
    const qreal decay = std::exp(-kFriction * t);
    QPointF target = m_start + m_velocity / kFriction * (1 - decay);

    bool atBound = false;
    qreal min, max;
    if (m_velocity.y() != 0) {
        range(m_target, true, min, max);
        if (target.y() <= min || target.y() >= max)
            atBound = true;
        target.setY(qBound(min, target.y(), max));
    }
    if (m_velocity.x() != 0) {
        range(m_target, false, min, max);
        if (target.x() <= min || target.x() >= max)
            atBound = true;
        target.setX(qBound(min, target.x(), max));
    }

    if (m_velocity.x() != 0)
        m_target->setProperty("contentX", target.x());
    if (m_velocity.y() != 0)
        m_target->setProperty("contentY", target.y());
    m_lastSet = QPointF(m_target->property("contentX").toReal(),
                        m_target->property("contentY").toReal());

    const qreal speed = std::hypot(m_velocity.x(), m_velocity.y()) * decay;
    if (atBound || speed < kStopVelocity)
        stopGlide();
}
