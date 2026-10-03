#pragma once

#include "model/error.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>

namespace Harpoon {

// Trust anchors for the public-good Sigstore instance: Fulcio certificate
// chains and Rekor log keys, in the format of Sigstore's trusted_root.json.
struct SigstoreTrust
{
    struct Authority
    {
        QList<QByteArray> chainDer; // as listed: intermediate(s) first, root last
        QDateTime start;
        QDateTime end; // invalid: still valid
    };
    struct Log
    {
        QByteArray keyId;        // raw bytes (sha256 of the DER public key)
        QByteArray publicKeyDer; // SubjectPublicKeyInfo
        QDateTime start;
        QDateTime end;
    };
    QList<Authority> authorities;
    QList<Log> logs;

    static Result<SigstoreTrust> fromJson(const QByteArray &json);
    // The copy shipped with Harpoon (core/data/sigstore-trusted-root.json).
    static const SigstoreTrust &builtIn();
};

// What a verified bundle proves.
struct VerifiedBundle
{
    QJsonObject statement;    // the signed in-toto statement
    QString sourceRepository; // e.g. https://github.com/owner/repo (Fulcio extension)
    QString issuer;           // OIDC issuer, e.g. https://token.actions.githubusercontent.com
    QString san;              // certificate identity, e.g. the workflow URL
    QDateTime signedAt;       // Rekor's integrated time
};

enum class BundleKind {
    PublicGood,     // Fulcio certificate plus Rekor entries: verifiable here
    GitHubInstance, // GitHub's own Sigstore (private repositories): RFC 3161 timestamps
    Unknown,
};
BundleKind sigstoreBundleKind(const QJsonObject &bundle);

// Verifies a Sigstore bundle (v0.1-v0.3, DSSE envelope, Rekor v1 entry):
//  1. the Rekor entry under a trusted log key: its signed entry timestamp
//     (which also vouches for the signing time), and its inclusion proof and
//     signed checkpoint when present;
//  2. that the entry records this envelope (payload hash, signature, key);
//  3. the Fulcio certificate chain at the time the entry was integrated;
//  4. the DSSE signature over the payload with the certificate's key.
// The certificate identity is returned, not checked; see
// checkGitHubWorkflowIdentity(). Certificate transparency SCTs are not checked.
Result<VerifiedBundle> verifySigstoreBundle(const QJsonObject &bundle, const SigstoreTrust &trust);

// The bundle was signed by a GitHub Actions workflow running in the
// repository at repositoryUrl (e.g. https://github.com/owner/repo), as named
// by the certificate's source repository extension.
Error checkGitHubWorkflowIdentity(const VerifiedBundle &bundle, const QString &repositoryUrl);

// "https://<host>/<owner>/<repo>" for comparing repository URLs: https,
// lower-case host, github.com for www.github.com, no trailing slash or .git.
QString canonicalRepositoryUrl(const QString &repositoryUrl);

// The source repository extension of a bundle's signing certificate, read
// WITHOUT verifying anything (for bundles that cannot be verified here).
QString bundleSourceRepository(const QJsonObject &bundle);

} // namespace Harpoon
