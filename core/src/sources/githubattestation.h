#pragma once

#include "model/error.h"
#include "net/httptransport.h"

#include <functional>

namespace Harpoon {

// GitHub artifact attestations (build provenance) for a downloaded file.
//
//   GET {api}/attestations/sha256:{digest}
//
// "Verified" means GitHub holds an attestation for this repository whose
// in-toto statement names a subject with exactly this sha256: the file was
// built by the repository's own workflow (e.g. actions/attest-build-provenance).
// The Sigstore signature in the bundle is NOT verified locally; Harpoon trusts
// GitHub's API over TLS for that, like ObtainX does.
enum class AttestationStatus {
    Verified, // an attestation with a matching subject digest exists
    Missing,  // the repository has no attestation for this file
    Error,    // could not tell (network, auth, unexpected response)
};

struct AttestationResult
{
    AttestationStatus status = AttestationStatus::Error;
    QString message;
};

// apiBase: GitHubSource::apiBaseUrl(); token may be empty (public repos).
void checkGitHubAttestation(HttpTransport &transport, const QString &apiBase, const QString &token,
                            const QString &sha256, std::function<void(const AttestationResult &)> done);

// Parses an attestations response for sha256 (exposed for tests).
AttestationResult parseAttestations(const QByteArray &json, const QString &sha256);

QString attestationStatusName(AttestationStatus status);

} // namespace Harpoon
