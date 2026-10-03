#include "faketransport.h"

#include "app/appchecker.h"
#include "sources/githubattestation.h"
#include "sources/sourceregistry.h"
#include "verify/sigstore.h"

#include <QFile>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace Harpoon;

namespace {
const QString kDigest = QStringLiteral("aa11bb22cc33dd44ee55ff6600778899aa11bb22cc33dd44ee55ff6600778899");
const QString kApi = QStringLiteral("https://api.github.com/repos/me/tool");
const QString kRepo = QStringLiteral("https://github.com/me/tool");

QByteArray fixture(const char *name)
{
    QFile f(QStringLiteral(HARPOON_FIXTURE_DIR "/sigstore/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return f.readAll();
}

QByteArray response(const QJsonObject &bundle)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("attestations"),
                                      QJsonArray{QJsonObject{{QStringLiteral("bundle"), bundle}}}}})
        .toJson();
}

AttestationResult parseFor(const QByteArray &json, const QString &digest = kDigest,
                           const SignerPolicy &policy = SignerPolicy())
{
    return parseAttestations(json, QStringLiteral("sha256"), digest, kRepo, policy, SigstoreTrust::builtIn());
}

// A self-signed certificate whose source repository extension
// (1.3.6.1.4.1.57264.1.12) is https://github.com/me/tool.
const char *kMeToolCert = "MIIBnzCCAUWgAwIBAgIUXrPB9xdOyNK4kB+SQCvNC+Z5MWYwCgYIKoZIzj0EAwIwDzENMAsGA1UECgwEVGVzdDAeFw0yNjEwMDMwMDU1NTBaFw0yNjEwMDQwMDU1NTBaMA8xDTALBgNVBAoMBFRlc3QwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQ8OAvA0aVdqCPsz8mxUp+CZU1mqlJ3HwkNwKn+rANmZXbqWFLhdhdEBGWizmUzIFN60iPnytSqR21WsGtuBc7co38wfTAdBgNVHQ4EFgQU/fR6dSCb6YmROGNIA+rWkoNVimkwHwYDVR0jBBgwFoAU/fR6dSCb6YmROGNIA+rWkoNVimkwDwYDVR0TAQH/BAUwAwEB/zAqBgorBgEEAYO/MAEMBBwMGmh0dHBzOi8vZ2l0aHViLmNvbS9tZS90b29sMAoGCCqGSM49BAMCA0gAMEUCIAfSlIUwuLnQQPyMZFfDiKdl7fCff0L+CXiU1SM992h1AiEA2j/hYROY05SZ1P0+uqzzxL09kIrZtE1NjZH3OGF8Dy4=";

// An attestations response as GitHub's own Sigstore instance (private
// repositories) signs them: one in-toto statement naming `digests`. The
// signature is not checked locally for these, so a fake one is enough; only
// the certificate's repository is compared.
QByteArray attestations(const QStringList &digests, const char *certificate = kMeToolCert)
{
    QJsonArray subjects;
    for (const QString &d : digests)
        subjects.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("tool.rpm")},
                                    {QStringLiteral("digest"), QJsonObject{{QStringLiteral("sha256"), d}}}});
    const QJsonObject statement{{QStringLiteral("_type"), QStringLiteral("https://in-toto.io/Statement/v1")},
                                {QStringLiteral("subject"), subjects},
                                {QStringLiteral("predicateType"), QStringLiteral("https://slsa.dev/provenance/v1")}};
    const QByteArray payload = QJsonDocument(statement).toJson(QJsonDocument::Compact).toBase64();
    const QJsonObject bundle{
        {QStringLiteral("dsseEnvelope"),
         QJsonObject{{QStringLiteral("payload"), QString::fromLatin1(payload)},
                     {QStringLiteral("payloadType"), QStringLiteral("application/vnd.in-toto+json")}}},
        {QStringLiteral("verificationMaterial"),
         QJsonObject{{QStringLiteral("timestampVerificationData"),
                      QJsonObject{{QStringLiteral("rfc3161Timestamps"), QJsonArray{QJsonObject()}}}},
                     {QStringLiteral("certificate"),
                      QJsonObject{{QStringLiteral("rawBytes"), QString::fromLatin1(certificate)}}}}}};
    return QJsonDocument(QJsonObject{{QStringLiteral("attestations"),
                                      QJsonArray{QJsonObject{{QStringLiteral("bundle"), bundle}}}}})
        .toJson();
}

AttestationResult lookup(FakeTransport &t)
{
    AttestationResult out;
    checkGitHubAttestation(t, kApi, kRepo, SignerPolicy(), QString(), kDigest, [&](const AttestationResult &r) { out = r; });
    return out;
}

AppInstaller::Verification runVerifier(const AppInstaller::Verifier &v, const QStringList &sha256s)
{
    AppInstaller::Verification out;
    bool called = false;
    QStringList files;
    for (int i = 0; i < sha256s.size(); ++i)
        files << QStringLiteral("/cache/0123456789ab-file%1.rpm").arg(i); // as AppInstaller names them
    v(App(), files, sha256s, [&](const AppInstaller::Verification &r) {
        out = r;
        called = true;
    });
    if (!called)
        qFatal("verifier did not finish");
    return out;
}
} // namespace

class TestAttestation : public QObject
{
    Q_OBJECT
private slots:
    void parse()
    {
        QCOMPARE(parseFor(attestations({kDigest})).status, AttestationStatus::VerifiedByGitHub);
        QCOMPARE(parseFor(attestations({kDigest.toUpper()})).status, AttestationStatus::VerifiedByGitHub);
        // An attestation for another file does not count.
        QCOMPARE(parseFor(attestations({QString(64, QLatin1Char('0'))})).status, AttestationStatus::Missing);
        QCOMPARE(parseFor("{\"attestations\": []}").status, AttestationStatus::Missing);
        QCOMPARE(parseFor("not json").status, AttestationStatus::Error);
        // A bundle without any verification material is not trusted.
        QCOMPARE(parseFor(response(QJsonObject{{QStringLiteral("dsseEnvelope"), QJsonObject()}})).status,
                 AttestationStatus::Error);
        // An unverifiable bundle must at least name this repository.
        QCOMPARE(parseFor(attestations({kDigest}, "")).status, AttestationStatus::Error);
        QCOMPARE(parseAttestations(attestations({kDigest}), QStringLiteral("sha256"), kDigest,
                                   QStringLiteral("https://github.com/someone/else"), SignerPolicy(), SigstoreTrust::builtIn())
                     .status,
                 AttestationStatus::Error);
        // www., http and a trailing slash name the same repository.
        QCOMPARE(parseAttestations(attestations({kDigest}), QStringLiteral("sha256"), kDigest,
                                   QStringLiteral("http://www.github.com/me/tool/"), SignerPolicy(), SigstoreTrust::builtIn())
                     .status,
                 AttestationStatus::VerifiedByGitHub);
    }

    void failedBundleIsNotHiddenByAnUncheckedOne()
    {
        // A public-good bundle that fails verification plus an unchecked
        // GitHub-instance bundle: the failure wins.
        QJsonObject forged = QJsonDocument::fromJson(fixture("sigstore-js-2.1.0-bundle.json")).object();
        QJsonObject envelope = forged.value(QStringLiteral("dsseEnvelope")).toObject();
        envelope[QStringLiteral("payload")] = QStringLiteral("e30="); // "{}"
        forged[QStringLiteral("dsseEnvelope")] = envelope;
        const QJsonArray unchecked = QJsonDocument::fromJson(attestations({kDigest})).object()
                                         .value(QStringLiteral("attestations")).toArray();
        const QByteArray both = QJsonDocument(QJsonObject{
            {QStringLiteral("attestations"),
             QJsonArray{QJsonObject{{QStringLiteral("bundle"), forged}}, unchecked.at(0)}}}).toJson();
        QCOMPARE(parseFor(both).status, AttestationStatus::Error);
    }

    void parseVerifiesSignaturesLocally()
    {
        // A real public-good bundle (npm provenance for sigstore-js; its
        // subject has a sha512 digest).
        const QJsonObject bundle = QJsonDocument::fromJson(fixture("sigstore-js-2.1.0-bundle.json")).object();
        const QString sha512 = QStringLiteral("90f223f992e4c88dd068cd2a5fc57f9d2b30798343dd6e38f29c240e04ba090e"
                                              "f831f84490847c4e82b9232c78e8a258463b1e55c0f7469f730265008fa6633f");
        const QString repo = QStringLiteral("https://github.com/sigstore/sigstore-js");
        AttestationResult r = parseAttestations(response(bundle), QStringLiteral("sha512"), sha512, repo,
                                                SignerPolicy(), SigstoreTrust::builtIn());
        QCOMPARE(r.status, AttestationStatus::Verified);
        QVERIFY(r.message.contains(QLatin1String("release.yml")));

        // Signed by another repository's workflow.
        r = parseAttestations(response(bundle), QStringLiteral("sha512"), sha512, kRepo, SignerPolicy(), SigstoreTrust::builtIn());
        QCOMPARE(r.status, AttestationStatus::Error);
        QVERIFY(r.message.contains(QLatin1String("sigstore-js")));

        // A forged statement under the real signature.
        QJsonObject forged = bundle;
        QJsonObject envelope = forged.value(QStringLiteral("dsseEnvelope")).toObject();
        QJsonObject statement = QJsonDocument::fromJson(QByteArray::fromBase64(
                                                            envelope.value(QStringLiteral("payload")).toString().toLatin1()))
                                    .object();
        QJsonArray subjects{QJsonObject{{QStringLiteral("name"), QStringLiteral("tool.rpm")},
                                        {QStringLiteral("digest"), QJsonObject{{QStringLiteral("sha256"), kDigest}}}}};
        statement[QStringLiteral("subject")] = subjects;
        envelope[QStringLiteral("payload")] =
            QString::fromLatin1(QJsonDocument(statement).toJson(QJsonDocument::Compact).toBase64());
        forged[QStringLiteral("dsseEnvelope")] = envelope;
        r = parseAttestations(response(forged), QStringLiteral("sha256"), kDigest, repo, SignerPolicy(), SigstoreTrust::builtIn());
        QCOMPARE(r.status, AttestationStatus::Error);

        // Not an attestation for this file.
        r = parseAttestations(response(bundle), QStringLiteral("sha256"), kDigest, repo, SignerPolicy(), SigstoreTrust::builtIn());
        QCOMPARE(r.status, AttestationStatus::Missing);
    }

    void signerPolicy()
    {
        // The npm bundle was signed by .github/workflows/release.yml on refs/heads/main.
        const QJsonObject bundle = QJsonDocument::fromJson(fixture("sigstore-js-2.1.0-bundle.json")).object();
        const QString sha512 = QStringLiteral("90f223f992e4c88dd068cd2a5fc57f9d2b30798343dd6e38f29c240e04ba090e"
                                              "f831f84490847c4e82b9232c78e8a258463b1e55c0f7469f730265008fa6633f");
        const QString repo = QStringLiteral("https://github.com/sigstore/sigstore-js");
        auto status = [&](const QString &workflow, const QString &ref) {
            SignerPolicy policy;
            policy.workflow = workflow;
            policy.refPattern = ref;
            return parseAttestations(response(bundle), QStringLiteral("sha512"), sha512, repo, policy,
                                     SigstoreTrust::builtIn());
        };
        QCOMPARE(status(QStringLiteral("release.yml"), QString()).status, AttestationStatus::Verified);
        QCOMPARE(status(QStringLiteral(".github/workflows/release.yml"), QStringLiteral("refs/heads/main")).status,
                 AttestationStatus::Verified);
        const AttestationResult wrongWorkflow = status(QStringLiteral("ci.yml"), QString());
        QCOMPARE(wrongWorkflow.status, AttestationStatus::Error);
        QVERIFY(wrongWorkflow.message.contains(QLatin1String("ci.yml")));
        const AttestationResult wrongRef = status(QString(), QStringLiteral("refs/tags/.*"));
        QCOMPARE(wrongRef.status, AttestationStatus::Error);
        QVERIFY(wrongRef.message.contains(QLatin1String("refs/heads/main")));

        // An unverifiable bundle that claims no ref cannot satisfy a ref rule.
        SignerPolicy tagsOnly;
        tagsOnly.refPattern = QStringLiteral("refs/tags/.*");
        QCOMPARE(parseFor(attestations({kDigest}), kDigest, tagsOnly).status, AttestationStatus::Error);
    }

    void lookupOutcomes()
    {
        const QString url = kApi + QStringLiteral("/attestations/sha256:") + kDigest + QStringLiteral("?per_page=100");
        FakeTransport ok;
        ok.respondJson(url, attestations({kDigest}));
        QCOMPARE(lookup(ok).status, AttestationStatus::VerifiedByGitHub);
        QCOMPARE(ok.requests.first().url, url);

        FakeTransport missing; // unknown URL -> 404
        QCOMPARE(lookup(missing).status, AttestationStatus::Missing);

        FakeTransport refused;
        refused.respondJson(url, "{}", 403);
        const AttestationResult r = lookup(refused);
        QCOMPARE(r.status, AttestationStatus::Error);
        QVERIFY(r.message.contains(QLatin1String("token")));
    }

    void verifierModes()
    {
        SourceRegistry registry;
        FakeTransport t;
        const QString okDigest = kDigest;
        const QString badDigest = QString(64, QLatin1Char('b'));
        t.respondJson(kApi + QStringLiteral("/attestations/sha256:") + okDigest + QStringLiteral("?per_page=100"),
                      attestations({okDigest}));
        DeviceInfo device;
        device.arch = QStringLiteral("aarch64");
        AppChecker checker(registry, t, device);
        checker.setSourceConfig(QStringLiteral("GitHub"), {{QStringLiteral("token"), QStringLiteral("tok")}});

        App app = App::fromUrl(QStringLiteral("https://github.com/me/tool"));
        QVERIFY(!checker.verifier(app)); // off by default

        app.settings.set(Keys::githubBuildVerificationMode, QStringLiteral("audit"));
        AppInstaller::Verifier audit = checker.verifier(app);
        QVERIFY(audit);
        AppInstaller::Verification v = runVerifier(audit, {okDigest});
        QVERIFY(v.error.ok());
        QVERIFY(v.warnings.isEmpty());
        QCOMPARE(v.status, QStringLiteral("attestation:github"));
        QCOMPARE(t.requests.last().header("Authorization"), QByteArray("Bearer tok"));

        v = runVerifier(audit, {okDigest, badDigest});
        QVERIFY(v.error.ok());
        QCOMPARE(v.warnings.size(), 1);
        QVERIFY(v.warnings.first().contains(QLatin1String("file1.rpm: No build attestation")));
        QVERIFY(!v.warnings.first().contains(QLatin1String("0123456789ab")));
        QCOMPARE(v.status, QStringLiteral("attestation:missing"));

        app.settings.set(Keys::githubBuildVerificationMode, QStringLiteral("enforce"));
        v = runVerifier(checker.verifier(app), {badDigest});
        QCOMPARE(int(v.error.kind), int(Error::Verification));
        // Enforce needs a signature verified here; GitHub's word is not enough.
        v = runVerifier(checker.verifier(app), {okDigest});
        QCOMPARE(int(v.error.kind), int(Error::Verification));
        QCOMPARE(v.status, QStringLiteral("attestation:github"));
        // Nothing to verify is not verified.
        v = runVerifier(checker.verifier(app), {});
        QCOMPARE(int(v.error.kind), int(Error::Verification));

        // The app's signer rules reach the check.
        app.settings.set(Keys::githubBuildVerificationMode, QStringLiteral("audit"));
        app.settings.set(Keys::attestationRefRegEx, QStringLiteral("refs/tags/.*"));
        v = runVerifier(checker.verifier(app), {okDigest});
        QCOMPARE(v.status, QStringLiteral("attestation:error"));
        QVERIFY(v.warnings.first().contains(QLatin1String("refs/tags/.*")));

        // Only GitHub has attestations: audit has nothing to say, enforce refuses.
        App codeberg = App::fromUrl(QStringLiteral("https://codeberg.org/me/tool"));
        codeberg.settings.set(Keys::githubBuildVerificationMode, QStringLiteral("audit"));
        QVERIFY(!checker.verifier(codeberg));
        codeberg.settings.set(Keys::githubBuildVerificationMode, QStringLiteral("enforce"));
        v = runVerifier(checker.verifier(codeberg), {okDigest});
        QCOMPARE(int(v.error.kind), int(Error::Verification));
    }
};

QTEST_GUILESS_MAIN(TestAttestation)
#include "tst_attestation.moc"
