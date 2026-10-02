#include "sources/githubattestation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Harpoon {

QString attestationStatusName(AttestationStatus status)
{
    switch (status) {
    case AttestationStatus::Verified: return QStringLiteral("verified");
    case AttestationStatus::Missing: return QStringLiteral("missing");
    case AttestationStatus::Error: return QStringLiteral("error");
    }
    return QString();
}

AttestationResult parseAttestations(const QByteArray &json, const QString &sha256)
{
    AttestationResult result;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        result.message = QStringLiteral("Unexpected attestation response");
        return result;
    }
    const QJsonArray attestations = doc.object().value(QStringLiteral("attestations")).toArray();
    const QString wanted = sha256.toLower();
    for (const QJsonValue &a : attestations) {
        const QJsonObject envelope = a.toObject()
                                         .value(QStringLiteral("bundle")).toObject()
                                         .value(QStringLiteral("dsseEnvelope")).toObject();
        const QByteArray payload = QByteArray::fromBase64(envelope.value(QStringLiteral("payload")).toString().toLatin1());
        const QJsonObject statement = QJsonDocument::fromJson(payload).object();
        for (const QJsonValue &subject : statement.value(QStringLiteral("subject")).toArray()) {
            const QString digest = subject.toObject().value(QStringLiteral("digest")).toObject()
                                       .value(QStringLiteral("sha256")).toString().toLower();
            if (!digest.isEmpty() && digest == wanted) {
                result.status = AttestationStatus::Verified;
                result.message = QStringLiteral("Built by the repository's workflow (%1)")
                                     .arg(statement.value(QStringLiteral("predicateType")).toString());
                return result;
            }
        }
    }
    result.status = AttestationStatus::Missing;
    result.message = attestations.isEmpty() ? QStringLiteral("No build attestation for this file")
                                            : QStringLiteral("No attestation names this file's digest");
    return result;
}

void checkGitHubAttestation(HttpTransport &transport, const QString &apiBase, const QString &token,
                            const QString &sha256, std::function<void(const AttestationResult &)> done)
{
    HttpRequest request;
    request.url = apiBase + QStringLiteral("/attestations/sha256:") + sha256.toLower();
    request.setHeader("Accept", "application/vnd.github+json");
    if (!token.isEmpty())
        request.setHeader("Authorization", "Bearer " + token.toUtf8());
    transport.get(request, [sha256, done](const HttpResponse &response) {
        AttestationResult result;
        if (response.status == 200) {
            done(parseAttestations(response.body, sha256));
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
