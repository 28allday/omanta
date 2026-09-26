#include "Mounter.h"
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTest>

class TestMounter : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void passwordWaitsForAnAnswer();
    void questionRequiresAnExplicitValidChoice();
    void cancellingAbortsTheQuestion();
    void questionsAfterDestructionAreAborted();
};

static void ask(GMountOperation *operation)
{
    const char *choices[] = {"Cancel", "Connect", nullptr};
    g_signal_emit_by_name(operation, "ask-question", "Verify this server's fingerprint", choices);
}

struct Reply {
    int count = 0;
    GMountOperationResult result = G_MOUNT_OPERATION_UNHANDLED;
    void watch(GMountOperation *operation) {
        g_signal_connect(operation, "reply", G_CALLBACK(+[](GMountOperation *, GMountOperationResult result,
                                                            gpointer data) {
            auto *reply = static_cast<Reply *>(data);
            reply->result = result;
            ++reply->count;
        }), this);
    }
};

void TestMounter::passwordWaitsForAnAnswer()
{
    Mounter mounter;
    QSignalSpy prompts(&mounter, &Mounter::askPassword);
    GMountOperation *operation = mounter.createOperation();
    Reply reply;
    const auto cleanup = qScopeGuard([&] {
        g_signal_handlers_disconnect_by_data(operation, &reply);
        g_object_unref(operation);
    });
    reply.watch(operation);
    g_signal_emit_by_name(operation, "ask-password", "Password", "user", "", G_ASK_PASSWORD_NEED_PASSWORD);
    QCOMPARE(prompts.size(), 1);
    QTest::qWait(20);
    QCOMPARE(reply.count, 0);
    mounter.providePassword("user", "", "fixture", false, false);
    QCOMPARE(reply.count, 1);
    QCOMPARE(reply.result, G_MOUNT_OPERATION_HANDLED);
    QCOMPARE(QString::fromUtf8(g_mount_operation_get_password(operation)), QString("fixture"));
    QTest::qWait(20);
    QCOMPARE(reply.count, 1);
}

void TestMounter::questionRequiresAnExplicitValidChoice()
{
    Mounter mounter;
    QSignalSpy questions(&mounter, &Mounter::askQuestion);
    GMountOperation *operation = mounter.createOperation();
    Reply reply;
    const auto cleanup = qScopeGuard([&] {
        g_signal_handlers_disconnect_by_data(operation, &reply);
        g_object_unref(operation);
    });
    reply.watch(operation);
    ask(operation);
    QCOMPARE(questions.size(), 1);
    QCOMPARE(questions.first().at(1).toStringList(), (QStringList{"Cancel", "Connect"}));
    QTest::qWait(20);
    QCOMPARE(reply.count, 0);
    mounter.answerQuestion(-1);
    mounter.answerQuestion(2);
    mounter.providePassword("user", "", "password", false, false);
    QCOMPARE(reply.count, 0);
    mounter.answerQuestion(1);
    QCOMPARE(reply.count, 1);
    QCOMPARE(reply.result, G_MOUNT_OPERATION_HANDLED);
    QCOMPARE(g_mount_operation_get_choice(operation), 1);
    QTest::qWait(20);
    QCOMPARE(reply.count, 1);
    mounter.answerQuestion(0);
    QCOMPARE(reply.count, 1);
}

void TestMounter::cancellingAbortsTheQuestion()
{
    Mounter mounter;
    GMountOperation *operation = mounter.createOperation();
    Reply reply;
    const auto cleanup = qScopeGuard([&] {
        g_signal_handlers_disconnect_by_data(operation, &reply);
        g_object_unref(operation);
    });
    reply.watch(operation);
    ask(operation);
    mounter.cancelPassword();
    QCOMPARE(reply.count, 1);
    QCOMPARE(reply.result, G_MOUNT_OPERATION_ABORTED);
}

void TestMounter::questionsAfterDestructionAreAborted()
{
    auto *mounter = new Mounter;
    GMountOperation *operation = mounter->createOperation();
    Reply reply;
    const auto cleanup = qScopeGuard([&] {
        g_signal_handlers_disconnect_by_data(operation, &reply);
        g_object_unref(operation);
    });
    reply.watch(operation);
    delete mounter;
    ask(operation);
    QCOMPARE(reply.count, 1);
    QCOMPARE(reply.result, G_MOUNT_OPERATION_ABORTED);
}

QTEST_GUILESS_MAIN(TestMounter)
#include "tst_mounter.moc"
