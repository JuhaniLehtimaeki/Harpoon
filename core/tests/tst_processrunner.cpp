// SystemProcessRunner::runAsync() with real processes.

#include "pkg/processrunner.h"

#include <QElapsedTimer>
#include <QtTest>

#include <memory>

using namespace Harpoon;

class TestProcessRunner : public QObject
{
    Q_OBJECT

    struct Outcome
    {
        bool called = false;
        int calls = 0;
        ProcessResult result;
    };

    std::shared_ptr<Outcome> start(QObject *context, const QString &program, const QStringList &arguments,
                                   int timeoutMs = 15000)
    {
        auto outcome = std::make_shared<Outcome>();
        m_runner.runAsync(context, program, arguments, [outcome](const ProcessResult &r) {
            outcome->called = true;
            ++outcome->calls;
            outcome->result = r;
        }, timeoutMs);
        return outcome;
    }

    SystemProcessRunner m_runner;

private slots:
    void finishesWithoutBlocking()
    {
        QObject context;
        auto o = start(&context, QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("echo out; echo err >&2; exit 3")});
        QVERIFY(!o->called); // nothing happens until the event loop runs
        QTRY_VERIFY(o->called);
        QVERIFY(o->result.started);
        QCOMPARE(o->result.exitCode, 3);
        QCOMPARE(o->result.standardOutput, QByteArray("out\n"));
        QCOMPARE(o->result.standardError, QByteArray("err\n"));
        QTest::qWait(50);
        QCOMPARE(o->calls, 1);
    }

    void missingProgram()
    {
        QObject context;
        auto o = start(&context, QStringLiteral("/nonexistent/harpoon-test-program"), {});
        QTRY_VERIFY(o->called);
        QVERIFY(!o->result.started);
        QTest::qWait(50);
        QCOMPARE(o->calls, 1);
    }

    void timesOut()
    {
        QObject context;
        QElapsedTimer clock;
        clock.start();
        auto o = start(&context, QStringLiteral("sleep"), {QStringLiteral("10")}, 200);
        QTRY_VERIFY(o->called);
        QVERIFY(clock.elapsed() < 5000);
        QVERIFY(!o->result.started);
        QTest::qWait(100); // the kill's own "finished" is not reported again
        QCOMPARE(o->calls, 1);
    }

    void destroyedContextIsNotCalled()
    {
        auto context = std::make_unique<QObject>();
        auto o = start(context.get(), QStringLiteral("sleep"), {QStringLiteral("10")});
        QTest::qWait(50);
        context.reset(); // ends the process
        QTest::qWait(100);
        QVERIFY(!o->called);
    }
};

QTEST_GUILESS_MAIN(TestProcessRunner)
#include "tst_processrunner.moc"
