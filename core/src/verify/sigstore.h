#pragma once

#include "model/error.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>

namespace Harpoon {

// Trust anchors for the public-good Sigstore instance: Fulcio certificate
// chains, Rekor log keys and timestamp authorities, in the format of
// Sigstore's trusted_root.json.
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
    QList<Authority> timestampAuthorities; // RFC 3161; chains as for Fulcio

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
    QString sourceRef;        // e.g. refs/tags/v1.0 (Fulcio extension)
    QString buildConfigUri;   // the workflow started in the repository, e.g. https://github.com/o/r/.github/workflows/release.yml@refs/tags/v1.0
    QString buildSignerUri;   // the workflow that signed: the same, or a reusable workflow elsewhere
    QDateTime signedAt;       // Rekor's integrated time, else the earliest timestamp
};

enum class BundleKind {
    PublicGood,     // Fulcio certificate plus Rekor entries: verifiable here
    GitHubInstance, // GitHub's own Sigstore (private repositories): RFC 3161 timestamps
    Unknown,
};
BundleKind sigstoreBundleKind(const QJsonObject &bundle);

// Verifies a Sigstore bundle (v0.1-v0.3, DSSE envelope):
//  1. a transparency log entry under a trusted log key. A Rekor v1 entry
//     needs its signed entry timestamp (which also vouches for the signing
//     time) and, when present, its inclusion proof and signed checkpoint. A
//     Rekor v2 entry has no signing time and needs its inclusion proof and
//     checkpoint;
//  2. that the entry records this envelope (payload hash, signature, key);
//  3. every RFC 3161 timestamp in the bundle, from a trusted timestamp
//     authority, over the signature. A Rekor v2 entry needs at least one;
//  4. the Fulcio certificate chain at each of those times;
//  5. the DSSE signature over the payload with the certificate's key.
// The certificate identity is returned, not checked; see
// checkGitHubWorkflowIdentity(). Certificate transparency SCTs are not checked.
Result<VerifiedBundle> verifySigstoreBundle(const QJsonObject &bundle, const SigstoreTrust &trust);

// Steps of verifySigstoreBundle(), exposed for tests.
//
// The time an RFC 3161 timestamp (a TimeStampResp or a bare token, DER) from
// a trusted timestamp authority vouches that `signature` existed.
Result<QDateTime> verifySignedTimestamp(const QByteArray &der, const QByteArray &signature,
                                        const SigstoreTrust &trust);
// The proof of a transparency log entry (a tlogEntries item) under a trusted
// log key, without matching its body: the integrated time for a Rekor v1
// entry, an invalid QDateTime for a Rekor v2 entry.
Result<QDateTime> verifyTransparencyLogProof(const QJsonObject &entry, const SigstoreTrust &trust);

// The bundle was signed by a GitHub Actions workflow running in the
// repository at repositoryUrl (e.g. https://github.com/owner/repo), as named
// by the certificate's source repository extension.
Error checkGitHubWorkflowIdentity(const VerifiedBundle &bundle, const QString &repositoryUrl);

// Which workflow runs of the repository may sign; empty fields allow any.
struct SignerPolicy
{
    // The workflow that was started: a file name ("release.yml"), a path in
    // the repository (".github/workflows/release.yml"), or the full URL of a
    // reusable workflow that signed (https://github.com/o/r/.github/workflows/x.yml).
    QString workflow;
    // A regular expression the whole source ref must match, e.g. "refs/tags/.*".
    QString refPattern;

    bool isEmpty() const { return workflow.trimmed().isEmpty() && refPattern.trimmed().isEmpty(); }
};

// The bundle's signing workflow and ref satisfy the policy. Call after
// checkGitHubWorkflowIdentity().
Error checkSignerPolicy(const VerifiedBundle &bundle, const SignerPolicy &policy);

// "https://<host>/<owner>/<repo>" for comparing repository URLs: https,
// lower-case host, github.com for www.github.com, no trailing slash or .git.
QString canonicalRepositoryUrl(const QString &repositoryUrl);

// The identity fields (issuer, SAN and the Fulcio extensions) of a bundle's
// signing certificate, read WITHOUT verifying anything (for bundles that
// cannot be verified here). statement and signedAt stay empty.
VerifiedBundle bundleClaimedIdentity(const QJsonObject &bundle);

} // namespace Harpoon
