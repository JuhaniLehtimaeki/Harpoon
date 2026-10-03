#pragma once

#include "model/error.h"
#include "net/httptransport.h"

#include <functional>

namespace Harpoon {

struct SigstoreTrust;
struct SignerPolicy;

// GitHub artifact attestations (build provenance) for a downloaded file.
//
//   GET {api}/attestations/sha256:{digest}
//
// Each returned Sigstore bundle is checked here: for public repositories the
// signature, certificate chain and transparency log entry are verified
// locally, and the signing certificate must name a GitHub Actions workflow of
// the app's own repository (and, when the app restricts it, the expected
// workflow and ref; see SignerPolicy). Private repositories are signed by GitHub's own
// Sigstore instance, which Harpoon cannot verify offline; those count as
// "checked by GitHub" (Harpoon then trusts GitHub's API over TLS), which is
// not enough for the "enforce" mode.
enum class AttestationStatus {
    Verified,         // a locally verified provenance attestation names this file
    VerifiedByGitHub, // GitHub returned one for this repository, but its signature could not be checked here
    Missing,          // the repository has no attestation for this file
    Error,            // could not tell, or an attestation failed verification
};

struct AttestationResult
{
    AttestationStatus status = AttestationStatus::Error;
    QString message;
};

// apiBase: GitHubSource::apiBaseUrl(); repositoryUrl: the app's standard URL
// (https://github.com/owner/repo); token may be empty (public repos).
void checkGitHubAttestation(HttpTransport &transport, const QString &apiBase, const QString &repositoryUrl,
                            const SignerPolicy &policy, const QString &token, const QString &sha256,
                            std::function<void(const AttestationResult &)> done);

// Parses an attestations response for a file digest (exposed for tests).
// algorithm is the in-toto digest name, e.g. "sha256".
AttestationResult parseAttestations(const QByteArray &json, const QString &algorithm, const QString &digest,
                                    const QString &repositoryUrl, const SignerPolicy &policy,
                                    const SigstoreTrust &trust);

QString attestationStatusName(AttestationStatus status);

} // namespace Harpoon
