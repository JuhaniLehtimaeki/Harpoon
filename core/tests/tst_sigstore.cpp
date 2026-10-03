// Local Sigstore verification against real bundles (copied from the GitHub CLI's
// test data, cli/cli pkg/cmd/attestation/test/data).

#include "verify/sigstore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

using namespace Harpoon;

namespace {

QByteArray readFixture(const QString &name)
{
    QFile f(QStringLiteral(HARPOON_FIXTURE_DIR "/sigstore/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

QJsonObject bundle(const QString &name)
{
    return QJsonDocument::fromJson(readFixture(name)).object();
}

QJsonObject npmBundle()
{
    return bundle(QStringLiteral("sigstore-js-2.1.0-bundle.json"));
}

// Edits one field of the first transparency log entry.
QJsonObject withEntry(QJsonObject b, const std::function<void(QJsonObject &)> &edit)
{
    QJsonObject material = b.value(QStringLiteral("verificationMaterial")).toObject();
    QJsonArray entries = material.value(QStringLiteral("tlogEntries")).toArray();
    QJsonObject entry = entries.at(0).toObject();
    edit(entry);
    entries[0] = entry;
    material[QStringLiteral("tlogEntries")] = entries;
    b[QStringLiteral("verificationMaterial")] = material;
    return b;
}

QJsonObject withEnvelope(QJsonObject b, const QString &key, const QJsonValue &value)
{
    QJsonObject envelope = b.value(QStringLiteral("dsseEnvelope")).toObject();
    envelope[key] = value;
    b[QStringLiteral("dsseEnvelope")] = envelope;
    return b;
}

} // namespace

class TestSigstore : public QObject
{
    Q_OBJECT

    SigstoreTrust m_trust;

private slots:
    void initTestCase()
    {
        const auto trust = SigstoreTrust::fromJson(readFixture(QStringLiteral("trusted_root.json")));
        QVERIFY(trust.ok());
        m_trust = trust.value;
    }

    void builtInTrustedRootLoads()
    {
        const SigstoreTrust &builtIn = SigstoreTrust::builtIn();
        QVERIFY(!builtIn.authorities.isEmpty());
        QVERIFY(!builtIn.logs.isEmpty());
        // The shipped root verifies a real bundle too.
        QVERIFY(verifySigstoreBundle(npmBundle(), builtIn).ok());
    }

    void verifiesARealBundle()
    {
        QCOMPARE(sigstoreBundleKind(npmBundle()), BundleKind::PublicGood);
        const auto result = verifySigstoreBundle(npmBundle(), m_trust);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const VerifiedBundle &v = result.value;
        QCOMPARE(v.sourceRepository, QStringLiteral("https://github.com/sigstore/sigstore-js"));
        QCOMPARE(v.issuer, QStringLiteral("https://token.actions.githubusercontent.com"));
        QCOMPARE(v.san, QStringLiteral("https://github.com/sigstore/sigstore-js/.github/workflows/release.yml@refs/heads/main"));
        QCOMPARE(v.signedAt.toMSecsSinceEpoch() / 1000, qint64(1693323623));
        QCOMPARE(v.statement.value(QStringLiteral("predicateType")).toString(), QStringLiteral("https://slsa.dev/provenance/v1"));
        const QJsonObject subject = v.statement.value(QStringLiteral("subject")).toArray().at(0).toObject();
        QCOMPARE(subject.value(QStringLiteral("name")).toString(), QStringLiteral("pkg:npm/sigstore@2.1.0"));

        QVERIFY(checkGitHubWorkflowIdentity(v, QStringLiteral("https://github.com/sigstore/sigstore-js")).ok());
        QVERIFY(checkGitHubWorkflowIdentity(v, QStringLiteral("https://github.com/Sigstore/Sigstore-JS")).ok());
        QVERIFY(!checkGitHubWorkflowIdentity(v, QStringLiteral("https://github.com/sigstore/sigstore-go")).ok());
        QVERIFY(!checkGitHubWorkflowIdentity(v, QStringLiteral("https://github.com/evil/sigstore-js")).ok());
        QVERIFY(checkGitHubWorkflowIdentity(v, QStringLiteral("https://github.com/sigstore/sigstore-js/")).ok());
        QVERIFY(!checkGitHubWorkflowIdentity(v, QStringLiteral("https://github.com/sigstore/sigstore")).ok());
        QVERIFY(checkGitHubWorkflowIdentity(v, QStringLiteral("http://www.github.com/sigstore/sigstore-js.git")).ok());

        // Without the source repository extension there is no identity: the
        // SAN may name a reusable workflow of another repository.
        VerifiedBundle noSource = v;
        noSource.sourceRepository.clear();
        QVERIFY(!checkGitHubWorkflowIdentity(noSource, QStringLiteral("https://github.com/sigstore/sigstore-js")).ok());
    }

    void inclusionProof()
    {
        // The signed entry timestamp alone is enough...
        const QJsonObject setOnly = withEntry(npmBundle(), [](QJsonObject &e) { e.remove(QStringLiteral("inclusionProof")); });
        QVERIFY(verifySigstoreBundle(setOnly, m_trust).ok());
        // ...but an inclusion proof alone does not vouch for the signing time.
        const QJsonObject proofOnly = withEntry(npmBundle(), [](QJsonObject &e) { e.remove(QStringLiteral("inclusionPromise")); });
        QVERIFY(!verifySigstoreBundle(proofOnly, m_trust).ok());

        // A proof that is present must hold.
        const QJsonObject badPath = withEntry(npmBundle(), [](QJsonObject &e) {
            QJsonObject proof = e.value(QStringLiteral("inclusionProof")).toObject();
            QJsonArray hashes = proof.value(QStringLiteral("hashes")).toArray();
            hashes[3] = hashes.at(4);
            proof[QStringLiteral("hashes")] = hashes;
            e[QStringLiteral("inclusionProof")] = proof;
        });
        QVERIFY(!verifySigstoreBundle(badPath, m_trust).ok());
        const QJsonObject badCheckpoint = withEntry(npmBundle(), [](QJsonObject &e) {
            QJsonObject proof = e.value(QStringLiteral("inclusionProof")).toObject();
            QJsonObject checkpoint = proof.value(QStringLiteral("checkpoint")).toObject();
            checkpoint[QStringLiteral("envelope")] = checkpoint.value(QStringLiteral("envelope")).toString()
                                                         .replace(QStringLiteral("Timestamp: 1693323623528968756"),
                                                                  QStringLiteral("Timestamp: 1693323623528968757"));
            proof[QStringLiteral("checkpoint")] = checkpoint;
            e[QStringLiteral("inclusionProof")] = proof;
        });
        QVERIFY(!verifySigstoreBundle(badCheckpoint, m_trust).ok());
    }

    void rejectsTampering_data()
    {
        QTest::addColumn<QJsonObject>("bundle");
        const QJsonObject b = npmBundle();

        // A different statement under the original signature.
        QJsonObject statement = QJsonDocument::fromJson(QByteArray::fromBase64(
            b.value(QStringLiteral("dsseEnvelope")).toObject().value(QStringLiteral("payload")).toString().toLatin1())).object();
        statement[QStringLiteral("predicateType")] = QStringLiteral("https://example.org/other");
        QTest::newRow("payload") << withEnvelope(b, QStringLiteral("payload"),
                                                 QString::fromLatin1(QJsonDocument(statement).toJson(QJsonDocument::Compact).toBase64()));
        QTest::newRow("payload type") << withEnvelope(b, QStringLiteral("payloadType"), QStringLiteral("text/plain"));

        QTest::newRow("integrated time") << withEntry(b, [](QJsonObject &e) {
            e[QStringLiteral("integratedTime")] = QStringLiteral("1693323700");
        });
        QTest::newRow("signed entry timestamp") << withEntry(b, [](QJsonObject &e) {
            e[QStringLiteral("inclusionPromise")] = QJsonObject{
                {QStringLiteral("signedEntryTimestamp"),
                 QStringLiteral("MEUCIQCnaSJW0CMB6KO8HP+3mNk9t2v3Yc+zzpyfV7m29tg6jAIgZ6R54ZBBhlpXFE9e25TiNoh2fpVqMn/FQr5zz4z1TPE=")}};
            e.remove(QStringLiteral("inclusionProof"));
        });
        QTest::newRow("untrusted log") << withEntry(b, [](QJsonObject &e) {
            e[QStringLiteral("logId")] = QJsonObject{{QStringLiteral("keyId"), QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")}};
        });
        QTest::newRow("no log entry") << withEntry(b, [](QJsonObject &e) { e = QJsonObject(); });

        QJsonObject noCert = b;
        QJsonObject material = noCert.value(QStringLiteral("verificationMaterial")).toObject();
        material.remove(QStringLiteral("certificate"));
        noCert[QStringLiteral("verificationMaterial")] = material;
        QTest::newRow("no certificate") << noCert;
    }

    void rejectsTampering()
    {
        QFETCH(QJsonObject, bundle);
        const auto result = verifySigstoreBundle(bundle, m_trust);
        QVERIFY(!result.ok());
        QCOMPARE(int(result.error.kind), int(Error::Verification));
    }

    void rejectsAnUntrustedAuthority()
    {
        // A trusted root whose CAs did not issue the certificate.
        QJsonObject root = QJsonDocument::fromJson(readFixture(QStringLiteral("trusted_root.json"))).object();
        QJsonArray cas = root.value(QStringLiteral("certificateAuthorities")).toArray();
        for (int i = 0; i < cas.size(); ++i) {
            QJsonObject ca = cas.at(i).toObject();
            ca[QStringLiteral("validFor")] = QJsonObject{{QStringLiteral("start"), QStringLiteral("2030-01-01T00:00:00Z")}};
            cas[i] = ca;
        }
        root[QStringLiteral("certificateAuthorities")] = cas;
        const auto trust = SigstoreTrust::fromJson(QJsonDocument(root).toJson());
        QVERIFY(trust.ok());
        QVERIFY(!verifySigstoreBundle(npmBundle(), trust.value).ok());
    }

    void githubInstanceBundles()
    {
        // Private repositories use GitHub's own Sigstore with RFC 3161
        // timestamps instead of Rekor; these are not verified locally.
        const QJsonObject b = bundle(QStringLiteral("github_release_bundle.json"));
        QCOMPARE(sigstoreBundleKind(b), BundleKind::GitHubInstance);
        QVERIFY(!verifySigstoreBundle(b, m_trust).ok());
        QCOMPARE(sigstoreBundleKind(QJsonObject()), BundleKind::Unknown);
    }

    void invalidTrustedRoot()
    {
        QVERIFY(!SigstoreTrust::fromJson("{}").ok());
        QVERIFY(!SigstoreTrust::fromJson("not json").ok());
    }
};

QTEST_GUILESS_MAIN(TestSigstore)
#include "tst_sigstore.moc"
