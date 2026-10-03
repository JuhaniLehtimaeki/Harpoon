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

// Sigstore's conformance suite (sigstore/sigstore-conformance,
// test/assets/bundle-verify, Apache-2.0): a bundle and the trusted root it is
// checked against. Names ending in _fail must not verify.
struct Conformance
{
    QJsonObject bundle;
    SigstoreTrust trust;
};

Conformance conformance(const QString &name)
{
    const QString dir = QStringLiteral("conformance/") + name + QLatin1Char('/');
    const auto trust = SigstoreTrust::fromJson(readFixture(dir + QStringLiteral("trusted_root.json")));
    if (!trust.ok())
        qFatal("bad trusted root in %s", qPrintable(name));
    return {bundle(dir + QStringLiteral("bundle.sigstore.json")), trust.value};
}

QJsonObject material(const QJsonObject &bundle)
{
    return bundle.value(QStringLiteral("verificationMaterial")).toObject();
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

    void signerPolicy()
    {
        const VerifiedBundle v = verifySigstoreBundle(npmBundle(), m_trust).value;
        QCOMPARE(v.sourceRef, QStringLiteral("refs/heads/main"));
        QCOMPARE(v.buildConfigUri,
                 QStringLiteral("https://github.com/sigstore/sigstore-js/.github/workflows/release.yml@refs/heads/main"));
        QCOMPARE(v.buildSignerUri, v.buildConfigUri);

        auto allows = [&](const QString &workflow, const QString &ref) {
            SignerPolicy policy;
            policy.workflow = workflow;
            policy.refPattern = ref;
            return checkSignerPolicy(v, policy).ok();
        };
        QVERIFY(allows(QString(), QString()));
        QVERIFY(allows(QStringLiteral("release.yml"), QString()));
        QVERIFY(allows(QStringLiteral(".github/workflows/release.yml"), QString()));
        QVERIFY(allows(QStringLiteral("/.github/workflows/release.yml"), QString()));
        QVERIFY(allows(QStringLiteral("https://github.com/sigstore/sigstore-js/.github/workflows/release.yml"), QString()));
        QVERIFY(!allows(QStringLiteral("ci.yml"), QString()));
        QVERIFY(!allows(QStringLiteral("other/release.yml"), QString()));
        QVERIFY(!allows(QStringLiteral("https://github.com/evil/sigstore-js/.github/workflows/release.yml"), QString()));
        QVERIFY(allows(QString(), QStringLiteral("refs/heads/main")));
        QVERIFY(allows(QString(), QStringLiteral("refs/heads/(main|master)")));
        // The whole ref must match.
        QVERIFY(!allows(QString(), QStringLiteral("refs/heads/ma")));
        QVERIFY(!allows(QString(), QStringLiteral("refs/tags/.*")));
        QVERIFY(!allows(QString(), QStringLiteral("([")));

        // A reusable workflow elsewhere signed; the repository's own workflow started it.
        VerifiedBundle reusable = v;
        reusable.buildSignerUri = QStringLiteral("https://github.com/slsa-framework/slsa-github-generator/"
                                                 ".github/workflows/generator.yml@refs/tags/v2.0.0");
        SignerPolicy byGenerator;
        byGenerator.workflow = QStringLiteral("https://github.com/slsa-framework/slsa-github-generator/"
                                              ".github/workflows/generator.yml");
        QVERIFY(checkSignerPolicy(reusable, byGenerator).ok());
        byGenerator.workflow = QStringLiteral("release.yml");
        QVERIFY(checkSignerPolicy(reusable, byGenerator).ok());
        QVERIFY(!checkSignerPolicy(v, SignerPolicy{QStringLiteral("https://github.com/slsa-framework/"
                                                                  "slsa-github-generator/.github/workflows/generator.yml"),
                                                   QString()}).ok());

        // Claims read without verifying.
        QCOMPARE(bundleClaimedIdentity(npmBundle()).sourceRef, v.sourceRef);
        QVERIFY(bundleClaimedIdentity(QJsonObject()).sourceRepository.isEmpty());
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

    void builtInRootHasRekorV2()
    {
        // Production's Rekor v2 log and timestamp authority are trusted.
        const SigstoreTrust &builtIn = SigstoreTrust::builtIn();
        QVERIFY(builtIn.logs.size() >= 2);
        QVERIFY(!builtIn.timestampAuthorities.isEmpty());
    }

    // DSSE bundles logged to Rekor v2: a hashedrekord 0.0.2 entry over the
    // envelope's pre-authentication encoding, an inclusion proof up to a
    // signed checkpoint, and an RFC 3161 timestamp for the time.
    void rekorV2Bundles_data()
    {
        QTest::addColumn<QString>("name");
        for (const char *name : {"rekor2-dsse-happy-path", "rekor2-dsse-invalid-sig_fail",
                                 "rekor2-dsse-mismatch-envelope_fail", "rekor2-dsse-mismatch-sig_fail",
                                 "intoto-tsa-timestamp-outside-cert-validity_fail"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }

    void rekorV2Bundles()
    {
        QFETCH(QString, name);
        const Conformance c = conformance(name);
        const auto result = verifySigstoreBundle(c.bundle, c.trust);
        if (name.endsWith(QLatin1String("_fail"))) {
            QVERIFY(!result.ok());
            qInfo("%s", qPrintable(result.error.message));
            return;
        }
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(sigstoreBundleKind(c.bundle), BundleKind::PublicGood);
        QCOMPARE(result.value.sourceRepository,
                 QStringLiteral("https://github.com/sigstore-conformance/extremely-dangerous-public-oidc-beacon"));
        QCOMPARE(result.value.signedAt, QDateTime(QDate(2026, 5, 13), QTime(19, 23, 33), Qt::UTC));
        QVERIFY(!result.value.statement.isEmpty());
    }

    void rekorV2NeedsProofAndTime()
    {
        const Conformance c = conformance(QStringLiteral("rekor2-dsse-happy-path"));
        // No timestamp: a v2 entry has no time of its own.
        QJsonObject noTimestamp = c.bundle;
        QJsonObject m = material(noTimestamp);
        m.remove(QStringLiteral("timestampVerificationData"));
        noTimestamp[QStringLiteral("verificationMaterial")] = m;
        QVERIFY(!verifySigstoreBundle(noTimestamp, c.trust).ok());
        // No inclusion proof: nothing shows the entry is in the log.
        QVERIFY(!verifySigstoreBundle(withEntry(c.bundle, [](QJsonObject &e) {
            e.remove(QStringLiteral("inclusionProof"));
        }), c.trust).ok());
        // The production root does not trust the staging log and authority.
        QVERIFY(!verifySigstoreBundle(c.bundle, SigstoreTrust::builtIn()).ok());
    }

    // Timestamps from conformance bundles signing a message; the stamped
    // data is the bundle's signature.
    void signedTimestamps_data()
    {
        QTest::addColumn<QString>("name");
        for (const char *name : {"rekor2-timestamp-with-embedded-cert", "rekor2-timestamp-without-embedded-cert",
                                 "rekor2-timestamp-with-expired-cert-chain", "trust-root-tsa-validity-end-inclusive",
                                 "rekor2-timestamp-outside-trust-root-tsa-validity_fail",
                                 "rekor2-timestamp-outside-tsa-cert-validity_fail",
                                 "rekor2-timestamp-payload-mismatch_fail",
                                 "rekor2-timestamp-untrusted-tsa-with-embedded-cert_fail",
                                 "rekor2-timestamp-untrusted-tsa-without-embedded-cert_fail"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }

    void signedTimestamps()
    {
        QFETCH(QString, name);
        const Conformance c = conformance(name);
        const QByteArray signature = QByteArray::fromBase64(c.bundle.value(QStringLiteral("messageSignature")).toObject()
                                                                .value(QStringLiteral("signature")).toString().toLatin1());
        const QByteArray timestamp = QByteArray::fromBase64(
            material(c.bundle).value(QStringLiteral("timestampVerificationData")).toObject()
                .value(QStringLiteral("rfc3161Timestamps")).toArray().at(0).toObject()
                .value(QStringLiteral("signedTimestamp")).toString().toLatin1());
        QVERIFY(!signature.isEmpty());
        QVERIFY(!timestamp.isEmpty());
        const auto result = verifySignedTimestamp(timestamp, signature, c.trust);
        QCOMPARE(result.ok(), !name.endsWith(QLatin1String("_fail")));
        if (!result.ok())
            qInfo("%s", qPrintable(result.error.message));
        if (result.ok()) {
            QVERIFY(result.value.isValid());
            // Another signature was not stamped.
            QVERIFY(!verifySignedTimestamp(timestamp, signature + "x", c.trust).ok());
        }
    }

    // Rekor v2 checkpoints, with witness cosignatures and broken notes.
    void checkpoints_data()
    {
        QTest::addColumn<QString>("name");
        for (const char *name : {"rekor2-checkpoint-cosigned", "rekor2-checkpoint-multiple-cosigs",
                                 "rekor2-checkpoint-origin-not-first", "rekor2-checkpoint-two-sigs-cosigned",
                                 "rekor2-checkpoint-two-sigs-from-origin",
                                 "rekor2-checkpoint-missing-log-signature_fail",
                                 "rekor2-checkpoint-missing-origin_fail", "rekor2-checkpoint-missing-root-hash_fail",
                                 "rekor2-checkpoint-missing-size_fail",
                                 "rekor2-checkpoint-no-matching-signature_fail", "rekor2-no-inclusion-proof_fail"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }

    void checkpoints()
    {
        QFETCH(QString, name);
        const Conformance c = conformance(name);
        const QJsonObject entry = material(c.bundle).value(QStringLiteral("tlogEntries")).toArray().at(0).toObject();
        const auto result = verifyTransparencyLogProof(entry, c.trust);
        QCOMPARE(result.ok(), !name.endsWith(QLatin1String("_fail")));
        if (!result.ok())
            qInfo("%s", qPrintable(result.error.message));
        if (result.ok())
            QVERIFY(!result.value.isValid()); // v2: no integrated time
    }

    void invalidTrustedRoot()
    {
        QVERIFY(!SigstoreTrust::fromJson("{}").ok());
        QVERIFY(!SigstoreTrust::fromJson("not json").ok());
    }
};

QTEST_GUILESS_MAIN(TestSigstore)
#include "tst_sigstore.moc"
