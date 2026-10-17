#include "KineticScroll.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>

#include <memory>

// Touchpad scrolling made to feel like GTK's. The
// swipes here are synthetic phased wheel events marked system-synthesized,
// which is how Qt's Wayland backend delivers a two-finger scroll.
class TestKinetic : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void fingerScrollUsesGtkSpeed();
    void flingGlidesThenRests();
    void pauseBeforeLiftDoesNotGlide();
    void touchingAgainStopsTheGlide();
    void clickStopsTheGlide();
    void mouseWheelIsLeftAlone();

private:
    void send(Qt::ScrollPhase phase, int dy, quint64 ms);
    void swipe(int dyPerEvent, int events);
    qreal contentY() const { return m_list->property("contentY").toReal(); }
    // Waits for the view to stop changing and returns where it settled.
    qreal settled();

    QQmlEngine m_engine;
    KineticScroll m_kinetic;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_list = nullptr;
    quint64 m_ms = 1000;
};

void TestKinetic::init()
{
    qApp->installEventFilter(&m_kinetic);
    QQmlComponent component(&m_engine);
    component.setData(R"(
        import QtQuick
        Window {
            width: 300; height: 300; visible: true
            ListView {
                objectName: "list"
                anchors.fill: parent
                boundsBehavior: Flickable.StopAtBounds
                model: 2000
                delegate: Rectangle { width: 300; height: 20 }
            }
        })", QUrl());
    m_window.reset(qobject_cast<QQuickWindow *>(component.create()));
    QVERIFY2(m_window, qPrintable(component.errorString()));
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    m_list = m_window->findChild<QQuickItem *>("list");
    QVERIFY(m_list);
    m_list->setProperty("contentY", 4000);
}

void TestKinetic::cleanup()
{
    qApp->removeEventFilter(&m_kinetic);
    m_window.reset();
}

void TestKinetic::send(Qt::ScrollPhase phase, int dy, quint64 ms)
{
    const QPointF at(150, 150);
    QWheelEvent event(at, m_window->mapToGlobal(at), QPoint(0, dy), QPoint(0, dy),
                      Qt::NoButton, Qt::NoModifier, phase, false,
                      Qt::MouseEventSynthesizedBySystem);
    event.setTimestamp(ms);
    QCoreApplication::sendEvent(m_window.get(), &event);
}

// Fingers moving up the pad, an event every 8ms: the page scrolls down.
void TestKinetic::swipe(int dyPerEvent, int events)
{
    send(Qt::ScrollBegin, 0, m_ms);
    for (int i = 0; i < events; ++i) {
        m_ms += 8;
        send(Qt::ScrollUpdate, dyPerEvent, m_ms);
    }
}

qreal TestKinetic::settled()
{
    qreal last = contentY();
    for (int quiet = 0; quiet < 10;) {
        QTest::qWait(30);
        const qreal now = contentY();
        quiet = qFuzzyCompare(now, last) ? quiet + 1 : 0;
        last = now;
    }
    return last;
}

void TestKinetic::fingerScrollUsesGtkSpeed()
{
    // The same swipe with and without the filter. Flickable holds back the
    // start of a drag either way, so compare the two rather than a sum.
    qApp->removeEventFilter(&m_kinetic);
    swipe(-40, 8);
    const qreal plain = settled() - 4000;
    send(Qt::ScrollEnd, 0, m_ms += 200);
    QVERIFY(plain > 0);

    qApp->installEventFilter(&m_kinetic);
    m_list->setProperty("contentY", 4000);
    m_ms += 1000;
    swipe(-40, 8);
    const qreal scaled = settled() - 4000;
    QVERIFY2(qAbs(scaled / plain - KineticScroll::kTouchpadFactor) < 0.15,
             qPrintable(QStringLiteral("%1 vs %2").arg(scaled).arg(plain)));
}

void TestKinetic::flingGlidesThenRests()
{
    swipe(-30, 10); // 75px per 8ms once scaled: ~9400px/s
    QTRY_VERIFY(contentY() > 4300);
    const qreal atLift = contentY();
    send(Qt::ScrollEnd, 0, m_ms + 4);
    const qreal rest = settled();
    // GTK's exponential glide travels v/4: ~2300px here.
    QVERIFY2(rest > atLift + 1800 && rest < atLift + 2800,
             qPrintable(QString::number(rest - atLift)));
}

void TestKinetic::pauseBeforeLiftDoesNotGlide()
{
    swipe(-30, 10);
    QTRY_VERIFY(contentY() > 4300);
    const qreal atLift = settled();
    send(Qt::ScrollEnd, 0, m_ms + 200); // held still, then lifted
    QCOMPARE(settled(), atLift);
}

void TestKinetic::touchingAgainStopsTheGlide()
{
    swipe(-30, 10);
    send(Qt::ScrollEnd, 0, m_ms + 4);
    const qreal atLift = contentY();
    QTRY_VERIFY(contentY() > atLift + 100);
    m_ms += 60;
    send(Qt::ScrollBegin, 0, m_ms);
    const qreal caught = contentY();
    QVERIFY(settled() < caught + 30); // at most a frame in flight
}

void TestKinetic::clickStopsTheGlide()
{
    swipe(-30, 10);
    send(Qt::ScrollEnd, 0, m_ms + 4);
    const qreal atLift = contentY();
    QTRY_VERIFY(contentY() > atLift + 100);
    QTest::mousePress(m_window.get(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
    const qreal caught = contentY();
    QVERIFY(settled() < caught + 30);
    QTest::mouseRelease(m_window.get(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
}

void TestKinetic::mouseWheelIsLeftAlone()
{
    const QPointF at(150, 150);
    QWheelEvent event(at, m_window->mapToGlobal(at), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    event.setTimestamp(m_ms += 1000); // Flickable ignores a click without time between
    QCoreApplication::sendEvent(m_window.get(), &event);
    const qreal mouseStep = settled() - 4000;
    QVERIFY(mouseStep > 0);

    // The same click without this filter moves exactly as far.
    qApp->removeEventFilter(&m_kinetic);
    m_list->setProperty("contentY", 4000);
    QWheelEvent again(at, m_window->mapToGlobal(at), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    again.setTimestamp(m_ms += 1000);
    QCoreApplication::sendEvent(m_window.get(), &again);
    QCOMPARE(settled() - 4000, mouseStep);
}

QTEST_MAIN(TestKinetic)
#include "tst_kinetic.moc"
