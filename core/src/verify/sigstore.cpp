#include "verify/sigstore.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QUrl>

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>

namespace Harpoon {

// Defined in the generated sigstoretrustedroot.cpp.
extern const char *const kSigstoreTrustedRootJson;

namespace {

using X509Ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using PKeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

Result<VerifiedBundle> fail(const QString &why)
{
    return Result<VerifiedBundle>::failure(Error::make(Error::Verification, why));
}

QByteArray fromB64(const QJsonValue &v)
{
    return QByteArray::fromBase64(v.toString().toLatin1());
}

QDateTime parseTime(const QJsonValue &v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODate).toUTC();
}

bool covers(const QDateTime &start, const QDateTime &end, const QDateTime &at)
{
    return (!start.isValid() || start <= at) && (!end.isValid() || at <= end);
}

X509Ptr parseCert(const QByteArray &der)
{
    const auto *p = reinterpret_cast<const unsigned char *>(der.constData());
    return X509Ptr(d2i_X509(nullptr, &p, der.size()), X509_free);
}

PKeyPtr parseKey(const QByteArray &spki)
{
    const auto *p = reinterpret_cast<const unsigned char *>(spki.constData());
    return PKeyPtr(d2i_PUBKEY(nullptr, &p, spki.size()), EVP_PKEY_free);
}

// ECDSA (SHA-256, or SHA-384 for P-384 keys) or Ed25519.
bool verifySignature(EVP_PKEY *key, const QByteArray &data, const QByteArray &signature)
{
    if (!key || signature.isEmpty())
        return false;
    const EVP_MD *md = nullptr;
    if (EVP_PKEY_base_id(key) == EVP_PKEY_EC)
        md = EVP_PKEY_bits(key) > 256 ? EVP_sha384() : EVP_sha256();
    else if (EVP_PKEY_base_id(key) != EVP_PKEY_ED25519)
        return false;
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestVerifyInit(ctx.get(), nullptr, md, nullptr, key) != 1)
        return false;
    return EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char *>(signature.constData()),
                            size_t(signature.size()), reinterpret_cast<const unsigned char *>(data.constData()),
                            size_t(data.size()))
           == 1;
}

QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

// DSSE pre-authentication encoding.
QByteArray pae(const QByteArray &type, const QByteArray &payload)
{
    return "DSSEv1 " + QByteArray::number(type.size()) + ' ' + type + ' ' + QByteArray::number(payload.size()) + ' '
           + payload;
}

QByteArray derFromPem(const QByteArray &pem)
{
    QByteArray b64;
    for (const QByteArray &line : pem.split('\n'))
        if (!line.startsWith("-----"))
            b64 += line.trimmed();
    return QByteArray::fromBase64(b64);
}

// The value of a Fulcio OID extension: DER UTF8String for 1.3.6.1.4.1.57264.1.8
// and later, raw bytes for the deprecated 1.3.6.1.4.1.57264.1.1-1.6.
QString extension(X509 *cert, const char *oid, bool der)
{
    std::unique_ptr<ASN1_OBJECT, decltype(&ASN1_OBJECT_free)> obj(OBJ_txt2obj(oid, 1), ASN1_OBJECT_free);
    if (!obj)
        return QString();
    const int index = X509_get_ext_by_OBJ(cert, obj.get(), -1);
    if (index < 0)
        return QString();
    const ASN1_OCTET_STRING *value = X509_EXTENSION_get_data(X509_get_ext(cert, index));
    const QByteArray bytes(reinterpret_cast<const char *>(ASN1_STRING_get0_data(value)), ASN1_STRING_length(value));
    if (!der)
        return QString::fromUtf8(bytes);
    // Short and long-form lengths of a UTF8String (tag 0x0c).
    if (bytes.size() < 2 || quint8(bytes.at(0)) != 0x0c)
        return QString();
    int length = quint8(bytes.at(1));
    int offset = 2;
    if (length & 0x80) {
        const int n = length & 0x7f;
        if (n < 1 || n > 2 || bytes.size() < 2 + n)
            return QString();
        length = 0;
        for (int i = 0; i < n; ++i)
            length = (length << 8) | quint8(bytes.at(2 + i));
        offset = 2 + n;
    }
    if (offset + length != bytes.size())
        return QString();
    return QString::fromUtf8(bytes.mid(offset, length));
}

QString sanUri(X509 *cert)
{
    QString out;
    auto *names = static_cast<GENERAL_NAMES *>(X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
    if (!names)
        return out;
    for (int i = 0; i < sk_GENERAL_NAME_num(names) && out.isEmpty(); ++i) {
        const GENERAL_NAME *name = sk_GENERAL_NAME_value(names, i);
        if (name->type == GEN_URI || name->type == GEN_EMAIL) {
            const ASN1_IA5STRING *s = name->type == GEN_URI ? name->d.uniformResourceIdentifier : name->d.rfc822Name;
            out = QString::fromUtf8(reinterpret_cast<const char *>(ASN1_STRING_get0_data(s)), ASN1_STRING_length(s));
        }
    }
    GENERAL_NAMES_free(names);
    return out;
}

const SigstoreTrust::Log *findLog(const SigstoreTrust &trust, const QByteArray &keyId)
{
    for (const auto &log : trust.logs)
        if (log.keyId == keyId)
            return &log;
    return nullptr;
}

// RFC 6962 / RFC 9162 inclusion proof.
bool verifyInclusion(qint64 index, qint64 size, const QByteArray &leafHash, const QList<QByteArray> &proof,
                     const QByteArray &root)
{
    if (index < 0 || size <= 0 || index >= size)
        return false;
    const quint64 diff = quint64(index) ^ quint64(size - 1);
    int inner = 0;
    while ((diff >> inner) != 0)
        ++inner;
    int border = 0;
    for (quint64 rest = quint64(index) >> inner; rest; rest >>= 1)
        border += int(rest & 1);
    if (proof.size() != inner + border)
        return false;
    QByteArray hash = leafHash;
    for (int i = 0; i < inner; ++i) {
        if (proof.at(i).size() != 32)
            return false;
        hash = ((quint64(index) >> i) & 1) ? sha256('\x01' + proof.at(i) + hash) : sha256('\x01' + hash + proof.at(i));
    }
    for (int i = inner; i < proof.size(); ++i) {
        if (proof.at(i).size() != 32)
            return false;
        hash = sha256('\x01' + proof.at(i) + hash);
    }
    return hash == root;
}

// A signed note ("origin\nsize\nroot\n...\n\n— name base64(hint+sig)\n")
// carrying the given tree size and root, signed by the log's key.
bool verifyCheckpoint(const QString &envelope, EVP_PKEY *key, qint64 size, const QByteArray &root)
{
    const QByteArray note = envelope.toUtf8();
    const int split = note.indexOf("\n\n");
    if (split < 0)
        return false;
    const QByteArray body = note.left(split + 1);
    const QList<QByteArray> lines = body.split('\n');
    if (lines.size() < 4 || lines.at(1) != QByteArray::number(size) || QByteArray::fromBase64(lines.at(2)) != root)
        return false;
    const QByteArray dash = QString(QChar(0x2014)).toUtf8() + ' ';
    for (const QByteArray &line : note.mid(split + 2).split('\n')) {
        if (!line.startsWith(dash))
            continue;
        const QList<QByteArray> parts = line.mid(dash.size()).split(' ');
        if (parts.size() != 2)
            continue;
        const QByteArray signature = QByteArray::fromBase64(parts.at(1));
        if (signature.size() > 4 && verifySignature(key, body, signature.mid(4)))
            return true;
    }
    return false;
}

// The canonicalized Rekor entry must record this envelope.
bool entryMatches(const QJsonObject &body, const QByteArray &payload, const QByteArray &signatureB64,
                  const QByteArray &certDer)
{
    const QString kind = body.value(QStringLiteral("kind")).toString();
    const QString version = body.value(QStringLiteral("apiVersion")).toString();
    const QJsonObject spec = body.value(QStringLiteral("spec")).toObject();
    const QString payloadHash = QString::fromLatin1(sha256(payload).toHex());

    QJsonObject hashObject;
    QJsonArray signatures;
    QString sigKey;
    QString keyKey;
    bool sigIsDoubleEncoded = false;
    if (kind == QLatin1String("intoto") && version == QLatin1String("0.0.2")) {
        const QJsonObject content = spec.value(QStringLiteral("content")).toObject();
        hashObject = content.value(QStringLiteral("payloadHash")).toObject();
        signatures = content.value(QStringLiteral("envelope")).toObject().value(QStringLiteral("signatures")).toArray();
        sigKey = QStringLiteral("sig");
        keyKey = QStringLiteral("publicKey");
        sigIsDoubleEncoded = true;
    } else if (kind == QLatin1String("dsse") && version == QLatin1String("0.0.1")) {
        hashObject = spec.value(QStringLiteral("payloadHash")).toObject();
        signatures = spec.value(QStringLiteral("signatures")).toArray();
        sigKey = QStringLiteral("signature");
        keyKey = QStringLiteral("verifier");
    } else {
        return false;
    }
    if (hashObject.value(QStringLiteral("algorithm")).toString() != QLatin1String("sha256")
        || hashObject.value(QStringLiteral("value")).toString().toLower() != payloadHash)
        return false;
    for (const QJsonValue &v : signatures) {
        const QJsonObject s = v.toObject();
        QByteArray sig = s.value(sigKey).toString().toLatin1();
        if (sigIsDoubleEncoded)
            sig = QByteArray::fromBase64(sig);
        if (QByteArray::fromBase64(sig) != QByteArray::fromBase64(signatureB64))
            continue;
        if (derFromPem(fromB64(s.value(keyKey))) == certDer)
            return true;
    }
    return false;
}

} // namespace

Result<SigstoreTrust> SigstoreTrust::fromJson(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    SigstoreTrust trust;
    for (const QJsonValue &v : root.value(QStringLiteral("certificateAuthorities")).toArray()) {
        const QJsonObject ca = v.toObject();
        Authority a;
        for (const QJsonValue &c : ca.value(QStringLiteral("certChain")).toObject().value(QStringLiteral("certificates")).toArray())
            a.chainDer << fromB64(c.toObject().value(QStringLiteral("rawBytes")));
        const QJsonObject valid = ca.value(QStringLiteral("validFor")).toObject();
        a.start = parseTime(valid.value(QStringLiteral("start")));
        a.end = parseTime(valid.value(QStringLiteral("end")));
        if (!a.chainDer.isEmpty())
            trust.authorities << a;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("tlogs")).toArray()) {
        const QJsonObject tlog = v.toObject();
        const QJsonObject key = tlog.value(QStringLiteral("publicKey")).toObject();
        Log l;
        l.keyId = fromB64(tlog.value(QStringLiteral("logId")).toObject().value(QStringLiteral("keyId")));
        l.publicKeyDer = fromB64(key.value(QStringLiteral("rawBytes")));
        const QJsonObject valid = key.value(QStringLiteral("validFor")).toObject();
        l.start = parseTime(valid.value(QStringLiteral("start")));
        l.end = parseTime(valid.value(QStringLiteral("end")));
        if (!l.keyId.isEmpty() && !l.publicKeyDer.isEmpty())
            trust.logs << l;
    }
    if (trust.authorities.isEmpty() || trust.logs.isEmpty())
        return Result<SigstoreTrust>::failure(Error::make(Error::Verification, QStringLiteral("Invalid Sigstore trusted root")));
    return Result<SigstoreTrust>::success(trust);
}

const SigstoreTrust &SigstoreTrust::builtIn()
{
    static const SigstoreTrust trust = fromJson(QByteArray(kSigstoreTrustedRootJson)).value;
    return trust;
}

BundleKind sigstoreBundleKind(const QJsonObject &bundle)
{
    const QJsonObject material = bundle.value(QStringLiteral("verificationMaterial")).toObject();
    if (!material.value(QStringLiteral("tlogEntries")).toArray().isEmpty())
        return BundleKind::PublicGood;
    if (!material.value(QStringLiteral("timestampVerificationData")).toObject().isEmpty())
        return BundleKind::GitHubInstance;
    return BundleKind::Unknown;
}

Result<VerifiedBundle> verifySigstoreBundle(const QJsonObject &bundle, const SigstoreTrust &trust)
{
    const QJsonObject material = bundle.value(QStringLiteral("verificationMaterial")).toObject();
    const QJsonObject envelope = bundle.value(QStringLiteral("dsseEnvelope")).toObject();
    if (envelope.isEmpty())
        return fail(QStringLiteral("The bundle has no DSSE envelope"));

    // The signing certificate: v0.3 "certificate", older "x509CertificateChain".
    QByteArray certDer = fromB64(material.value(QStringLiteral("certificate")).toObject().value(QStringLiteral("rawBytes")));
    if (certDer.isEmpty())
        certDer = fromB64(material.value(QStringLiteral("x509CertificateChain")).toObject()
                              .value(QStringLiteral("certificates")).toArray().at(0).toObject()
                              .value(QStringLiteral("rawBytes")));
    X509Ptr leaf = parseCert(certDer);
    if (!leaf)
        return fail(QStringLiteral("The bundle has no signing certificate"));

    const QByteArray payload = fromB64(envelope.value(QStringLiteral("payload")));
    const QByteArray payloadType = envelope.value(QStringLiteral("payloadType")).toString().toUtf8();
    const QJsonArray signatures = envelope.value(QStringLiteral("signatures")).toArray();
    if (payload.isEmpty() || signatures.size() != 1)
        return fail(QStringLiteral("The bundle's envelope must have a payload and one signature"));
    const QByteArray signatureB64 = signatures.first().toObject().value(QStringLiteral("sig")).toString().toLatin1();

    // 1-2. A transparency log entry for this envelope, under a trusted key.
    QDateTime signedAt;
    QString logProblem = QStringLiteral("The bundle has no transparency log entry");
    for (const QJsonValue &v : material.value(QStringLiteral("tlogEntries")).toArray()) {
        const QJsonObject entry = v.toObject();
        const QByteArray keyId = fromB64(entry.value(QStringLiteral("logId")).toObject().value(QStringLiteral("keyId")));
        const SigstoreTrust::Log *log = findLog(trust, keyId);
        if (!log) {
            logProblem = QStringLiteral("The transparency log is not trusted");
            continue;
        }
        const QByteArray bodyB64 = entry.value(QStringLiteral("canonicalizedBody")).toString().toLatin1();
        const QByteArray body = QByteArray::fromBase64(bodyB64);
        const QJsonObject bodyJson = QJsonDocument::fromJson(body).object();
        if (!entryMatches(bodyJson, payload, signatureB64, certDer)) {
            logProblem = QStringLiteral("The transparency log entry does not match the signature");
            continue;
        }
        const qint64 integrated = entry.value(QStringLiteral("integratedTime")).toString().toLongLong();
        const QDateTime time = QDateTime::fromMSecsSinceEpoch(integrated * 1000, Qt::UTC);
        if (integrated <= 0 || !covers(log->start, log->end, time)) {
            logProblem = QStringLiteral("The transparency log entry has no valid time");
            continue;
        }
        PKeyPtr logKey = parseKey(log->publicKeyDer);

        // The signed entry timestamp is what vouches for the integrated
        // time, so it is required; an inclusion proof, when present, must
        // hold as well.
        bool proven = false;
        const QByteArray set = fromB64(entry.value(QStringLiteral("inclusionPromise")).toObject()
                                           .value(QStringLiteral("signedEntryTimestamp")));
        if (!set.isEmpty()) {
            const QByteArray canonical = "{\"body\":\"" + bodyB64 + "\",\"integratedTime\":" + QByteArray::number(integrated)
                                         + ",\"logID\":\"" + keyId.toHex() + "\",\"logIndex\":"
                                         + QByteArray::number(entry.value(QStringLiteral("logIndex")).toString().toLongLong())
                                         + "}";
            proven = verifySignature(logKey.get(), canonical, set);
        }
        const QJsonObject proof = entry.value(QStringLiteral("inclusionProof")).toObject();
        if (proven && !proof.isEmpty()) {
            QList<QByteArray> hashes;
            for (const QJsonValue &h : proof.value(QStringLiteral("hashes")).toArray())
                hashes << fromB64(h);
            const qint64 size = proof.value(QStringLiteral("treeSize")).toString().toLongLong();
            const QByteArray root = fromB64(proof.value(QStringLiteral("rootHash")));
            proven = verifyInclusion(proof.value(QStringLiteral("logIndex")).toString().toLongLong(), size,
                                     sha256('\x00' + body), hashes, root)
                     && verifyCheckpoint(proof.value(QStringLiteral("checkpoint")).toObject()
                                             .value(QStringLiteral("envelope")).toString(),
                                         logKey.get(), size, root);
        }
        if (!proven) {
            logProblem = QStringLiteral("The transparency log entry's signature is invalid");
            continue;
        }
        signedAt = time;
        break;
    }
    if (!signedAt.isValid())
        return fail(logProblem);

    // 3. The certificate chains to a trusted Fulcio CA at signing time.
    bool chained = false;
    for (const auto &authority : trust.authorities) {
        if (!covers(authority.start, authority.end, signedAt))
            continue;
        std::unique_ptr<X509_STORE, decltype(&X509_STORE_free)> store(X509_STORE_new(), X509_STORE_free);
        std::unique_ptr<STACK_OF(X509), void (*)(STACK_OF(X509) *)>
            intermediates(sk_X509_new_null(), [](STACK_OF(X509) *s) { sk_X509_pop_free(s, X509_free); });
        bool ok = store && intermediates;
        for (int i = 0; ok && i < authority.chainDer.size(); ++i) {
            X509Ptr c = parseCert(authority.chainDer.at(i));
            if (!c) {
                ok = false;
            } else if (i == authority.chainDer.size() - 1) {
                ok = X509_STORE_add_cert(store.get(), c.get()) == 1;
            } else {
                ok = sk_X509_push(intermediates.get(), c.get()) > 0;
                if (ok)
                    c.release();
            }
        }
        if (!ok)
            continue;
        std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> ctx(X509_STORE_CTX_new(), X509_STORE_CTX_free);
        if (!ctx || X509_STORE_CTX_init(ctx.get(), store.get(), leaf.get(), intermediates.get()) != 1)
            continue;
        X509_VERIFY_PARAM_set_time(X509_STORE_CTX_get0_param(ctx.get()), time_t(signedAt.toMSecsSinceEpoch() / 1000));
        if (X509_verify_cert(ctx.get()) == 1) {
            chained = true;
            break;
        }
    }
    if (!chained)
        return fail(QStringLiteral("The signing certificate is not from a trusted Sigstore authority"));
    if (!(X509_get_extension_flags(leaf.get()) & EXFLAG_XKUSAGE) || !(X509_get_extended_key_usage(leaf.get()) & XKU_CODE_SIGN))
        return fail(QStringLiteral("The signing certificate is not for code signing"));

    // 4. The envelope signature.
    PKeyPtr leafKey(X509_get_pubkey(leaf.get()), EVP_PKEY_free);
    if (!verifySignature(leafKey.get(), pae(payloadType, payload), QByteArray::fromBase64(signatureB64)))
        return fail(QStringLiteral("The attestation's signature is invalid"));

    VerifiedBundle out;
    out.statement = QJsonDocument::fromJson(payload).object();
    out.sourceRepository = extension(leaf.get(), "1.3.6.1.4.1.57264.1.12", true);
    out.issuer = extension(leaf.get(), "1.3.6.1.4.1.57264.1.8", true);
    if (out.issuer.isEmpty())
        out.issuer = extension(leaf.get(), "1.3.6.1.4.1.57264.1.1", false);
    out.san = sanUri(leaf.get());
    out.signedAt = signedAt;
    if (payloadType != "application/vnd.in-toto+json" || out.statement.isEmpty())
        return fail(QStringLiteral("The attestation is not an in-toto statement"));
    return Result<VerifiedBundle>::success(out);
}

QString canonicalRepositoryUrl(const QString &repositoryUrl)
{
    const QUrl url(repositoryUrl.trimmed());
    QString host = url.host().toLower();
    if (host == QLatin1String("www.github.com"))
        host = QStringLiteral("github.com");
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (path.endsWith(QLatin1String(".git")))
        path.chop(4);
    const QString authority = url.port() > 0 && host != QLatin1String("github.com")
                                  ? host + QLatin1Char(':') + QString::number(url.port())
                                  : host;
    return QStringLiteral("https://") + authority + path;
}

QString bundleSourceRepository(const QJsonObject &bundle)
{
    const QJsonObject material = bundle.value(QStringLiteral("verificationMaterial")).toObject();
    QByteArray certDer = fromB64(material.value(QStringLiteral("certificate")).toObject().value(QStringLiteral("rawBytes")));
    if (certDer.isEmpty())
        certDer = fromB64(material.value(QStringLiteral("x509CertificateChain")).toObject()
                              .value(QStringLiteral("certificates")).toArray().at(0).toObject()
                              .value(QStringLiteral("rawBytes")));
    X509Ptr leaf = parseCert(certDer);
    return leaf ? extension(leaf.get(), "1.3.6.1.4.1.57264.1.12", true) : QString();
}

Error checkGitHubWorkflowIdentity(const VerifiedBundle &bundle, const QString &repositoryUrl)
{
    if (bundle.issuer != QLatin1String("https://token.actions.githubusercontent.com"))
        return Error::make(Error::Verification, QStringLiteral("The attestation was not made by GitHub Actions"));
    // The source repository extension names the repository whose workflow
    // ran; the SAN may name a reusable workflow in another repository, so it
    // is no substitute. Fulcio has set the extension for GitHub Actions since
    // before artifact attestations existed.
    if (bundle.sourceRepository.isEmpty())
        return Error::make(Error::Verification, QStringLiteral("The signing certificate names no source repository"));
    const QString expected = canonicalRepositoryUrl(repositoryUrl);
    if (canonicalRepositoryUrl(bundle.sourceRepository).compare(expected, Qt::CaseInsensitive) != 0)
        return Error::make(Error::Verification, QStringLiteral("The attestation was made for %1, not %2")
                                                    .arg(bundle.sourceRepository, expected));
    return Error();
}

} // namespace Harpoon
