#include "net/ratelimit.h"

#include <QtTest>

using namespace Harpoon;

class TestRateLimit : public QObject
{
    Q_OBJECT
private slots:
    void notLimited()
    {
        HttpResponse r;
        r.status = 404;
        r.body = "{\"message\":\"Not Found\"}";
        QVERIFY(rateLimitError(r, 0).ok());
    }

    void resetHeader()
    {
        HttpResponse r;
        r.status = 403;
        r.headers.insert("x-ratelimit-remaining", "0");
        r.headers.insert("x-ratelimit-reset", "1000"); // epoch seconds
        const Error e = rateLimitError(r, 100 * 1000);
        QCOMPARE(int(e.kind), int(Error::RateLimited));
        QCOMPARE(e.retryAfterMinutes, 15); // 900 s
    }

    void retryAfter()
    {
        HttpResponse r;
        r.status = 429;
        r.headers.insert("retry-after", "61");
        QCOMPARE(rateLimitError(r, 0).retryAfterMinutes, 2);
    }

    void defaultThirtyMinutes()
    {
        HttpResponse r;
        r.status = 200;
        r.body = "{\"message\":\"API rate limit exceeded for 1.2.3.4\"}";
        QCOMPARE(rateLimitError(r, 0).retryAfterMinutes, 30);
    }
};

QTEST_GUILESS_MAIN(TestRateLimit)
#include "tst_ratelimit.moc"
