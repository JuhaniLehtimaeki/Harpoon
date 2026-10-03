#include "sources/githubattestation.h"

#include "verify/sigstore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Harpoon {

namespace {

bool isProvenance(const QJsonObject &statement)
{
    const QString type = statement.value(QStringLiteral("predicateType")).toString();
    return type == QLatin1String("https://slsa.dev/provenance/v1")
           || type == QLatin1String("https://slsa.dev/provenance/v0.2");
}

bool namesDigest(const QJsonObject &statement, const QString &algorithm, const QString &digest)
{
    for (const QJsonValue &subject : statement.value(QStringLiteral("subject")).toArray()) {
        const QString value = subject.toObject().value(QStringLiteral("digest")).toObject().value(algorithm).toString();
        if (!value.isEmpty() && value.compare(digest, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

} // namespace

QString attestationStatusName(AttestationStatus status)
{
    switch (status) {
    case AttestationStatus::Verified: return QStringLiteral("verified");
    case AttestationStatus::VerifiedByGitHub: return QStringLiteral("github");
    case AttestationStatus::Missing: return QStringLiteral("missing");
    case AttestationStatus::Error: return QStringLiteral("error");
    }
    return QString();
}

AttestationResult parseAttestations(const QByteArray &json, const QString &algorithm, const QString &digest,
                                    const QString &repositoryUrl, const SignerPolicy &policy,
                                    const SigstoreTrust &trust)
{
    AttestationResult result;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        result.message = QStringLiteral("Unexpected attestation response");
        return result;
    }
    const QJsonArray attestations = doc.object().value(QStringLiteral("attestations")).toArray();
    bool byGitHub = false;
    QString problem;
    for (const QJsonValue &a : attestations) {
        const QJsonObject bundle = a.toObject().value(QStringLiteral("bundle")).toObject();
        switch (sigstoreBundleKind(bundle)) {
        case BundleKind::PublicGood: {
            const auto verified = verifySigstoreBundle(bundle, trust);
            if (!verified.ok()) {
                problem = verified.error.message;
                break;
            }
            if (!isProvenance(verified.value.statement) || !namesDigest(verified.value.statement, algorithm, digest))
                break;
            Error identity = checkGitHubWorkflowIdentity(verified.value, repositoryUrl);
            if (identity.ok())
                identity = checkSignerPolicy(verified.value, policy);
            if (!identity.ok()) {
                problem = identity.message;
                break;
            }
            result.status = AttestationStatus::Verified;
            result.message = QStringLiteral("Signed by %1").arg(verified.value.san);
            return result;
        }
        case BundleKind::GitHubInstance: {
            // Unverified here: only the claim GitHub's API makes over TLS.
            const QByteArray payload = QByteArray::fromBase64(bundle.value(QStringLiteral("dsseEnvelope")).toObject()
                                                                  .value(QStringLiteral("payload")).toString().toLatin1());
            const QJsonObject statement = QJsonDocument::fromJson(payload).object();
            if (!isProvenance(statement) || !namesDigest(statement, algorithm, digest))
                break;
            // Not proof (the certificate is not verified here), but a bundle
            // naming another repository, workflow or ref is certainly not
            // an acceptable one.
            const VerifiedBundle claimed = bundleClaimedIdentity(bundle);
            if (claimed.sourceRepository.isEmpty()
                || canonicalRepositoryUrl(claimed.sourceRepository)
                           .compare(canonicalRepositoryUrl(repositoryUrl), Qt::CaseInsensitive) != 0) {
                problem = QStringLiteral("The attestation was not made for this repository");
                break;
            }
            const Error claimedPolicy = checkSignerPolicy(claimed, policy);
            if (!claimedPolicy.ok()) {
                problem = claimedPolicy.message;
                break;
            }
            byGitHub = true;
            break;
        }
        case BundleKind::Unknown:
            problem = QStringLiteral("Unsupported attestation format");
            break;
        }
    }
    // A bundle that failed verification outweighs one that could not be
    // verified at all: never let an unchecked bundle hide a bad one.
    if (!problem.isEmpty()) {
        result.status = AttestationStatus::Error;
        result.message = problem;
    } else if (byGitHub) {
        result.status = AttestationStatus::VerifiedByGitHub;
        result.message = QStringLiteral("GitHub reports a build attestation; its signature can only be checked by GitHub");
    } else {
        result.status = AttestationStatus::Missing;
        result.message = attestations.isEmpty() ? QStringLiteral("No build attestation for this file")
                                                : QStringLiteral("No attestation names this file's digest");
    }
    return result;
}

void checkGitHubAttestation(HttpTransport &transport, const QString &apiBase, const QString &repositoryUrl,
                            const SignerPolicy &policy, const QString &token, const QString &sha256,
                            std::function<void(const AttestationResult &)> done)
{
    HttpRequest request;
    request.url = apiBase + QStringLiteral("/attestations/sha256:") + sha256.toLower() + QStringLiteral("?per_page=100");
    request.setHeader("Accept", "application/vnd.github+json");
    if (!token.isEmpty())
        request.setHeader("Authorization", "Bearer " + token.toUtf8());
    transport.get(request, [sha256, repositoryUrl, policy, done](const HttpResponse &response) {
        AttestationResult result;
        if (response.status == 200) {
            done(parseAttestations(response.body, QStringLiteral("sha256"), sha256, repositoryUrl, policy,
                                   SigstoreTrust::builtIn()));
            return;
        }
        if (response.status == 404) {
            result.status = AttestationStatus::Missing;
            result.message = QStringLiteral("No build attestation for this file");
        } else if (response.status == 401 || response.status == 403) {
            result.message = QStringLiteral("GitHub refused the attestation lookup; a token may be needed");
        } else {
            result.message = response.isNetworkError() ? response.networkError
                                                       : QStringLiteral("Attestation lookup failed: HTTP %1").arg(response.status);
        }
        done(result);
    });
}

} // namespace Harpoon
