#include "verify/sigstore.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/ts.h>
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

Result<QDateTime> failTime(const QString &why)
{
    return Result<QDateTime>::failure(Error::make(Error::Verification, why));
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

// Rekor v2 (0.0.2) entries: hashes as {algorithm: "SHA2_256", digest: base64}
// and keys as {x509Certificate: {rawBytes}}.
bool v2HashIs(const QJsonObject &hash, const QByteArray &expected)
{
    return hash.value(QStringLiteral("algorithm")).toString() == QLatin1String("SHA2_256")
           && fromB64(hash.value(QStringLiteral("digest"))) == expected;
}

bool v2SignatureIs(const QJsonObject &signature, const QByteArray &expected, const QByteArray &certDer)
{
    return fromB64(signature.value(QStringLiteral("content"))) == expected
           && fromB64(signature.value(QStringLiteral("verifier")).toObject().value(QStringLiteral("x509Certificate"))
                          .toObject().value(QStringLiteral("rawBytes")))
                  == certDer;
}

// The canonicalized Rekor entry must record this envelope.
bool entryMatches(const QJsonObject &body, const QByteArray &payloadType, const QByteArray &payload,
                  const QByteArray &signatureB64, const QByteArray &certDer)
{
    const QString kind = body.value(QStringLiteral("kind")).toString();
    const QString version = body.value(QStringLiteral("apiVersion")).toString();
    const QJsonObject spec = body.value(QStringLiteral("spec")).toObject();
    const QByteArray signature = QByteArray::fromBase64(signatureB64);

    if (version == QLatin1String("0.0.2") && kind == QLatin1String("hashedrekord")) {
        // Rekor v2 logs a DSSE envelope as a hashedrekord over its
        // pre-authentication encoding.
        const QJsonObject v2 = spec.value(QStringLiteral("hashedRekordV002")).toObject();
        return v2HashIs(v2.value(QStringLiteral("data")).toObject(), sha256(pae(payloadType, payload)))
               && v2SignatureIs(v2.value(QStringLiteral("signature")).toObject(), signature, certDer);
    }
    if (version == QLatin1String("0.0.2") && kind == QLatin1String("dsse")) {
        const QJsonObject v2 = spec.value(QStringLiteral("dsseV002")).toObject();
        if (!v2HashIs(v2.value(QStringLiteral("payloadHash")).toObject(), sha256(payload)))
            return false;
        for (const QJsonValue &v : v2.value(QStringLiteral("signatures")).toArray())
            if (v2SignatureIs(v.toObject(), signature, certDer))
                return true;
        return false;
    }

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
        if (QByteArray::fromBase64(sig) != signature)
            continue;
        if (derFromPem(fromB64(s.value(keyKey))) == certDer)
            return true;
    }
    return false;
}

// A store trusting the last certificate of a chain, with the others as
// untrusted intermediates, checking validity at `at`.
struct ChainStore
{
    std::unique_ptr<X509_STORE, decltype(&X509_STORE_free)> store{X509_STORE_new(), X509_STORE_free};
    std::unique_ptr<STACK_OF(X509), void (*)(STACK_OF(X509) *)> intermediates{
        sk_X509_new_null(), [](STACK_OF(X509) *s) { sk_X509_pop_free(s, X509_free); }};

    bool load(const QList<QByteArray> &chainDer, const QDateTime &at)
    {
        if (!store || !intermediates || chainDer.isEmpty())
            return false;
        for (int i = 0; i < chainDer.size(); ++i) {
            X509Ptr c = parseCert(chainDer.at(i));
            if (!c)
                return false;
            if (i == chainDer.size() - 1) {
                if (X509_STORE_add_cert(store.get(), c.get()) != 1)
                    return false;
            } else {
                if (sk_X509_push(intermediates.get(), c.get()) <= 0)
                    return false;
                c.release();
            }
        }
        X509_VERIFY_PARAM_set_time(X509_STORE_get0_param(store.get()), time_t(at.toMSecsSinceEpoch() / 1000));
        return true;
    }
};

// The certificate chains to a Fulcio CA trusted at `at`.
bool chainsToFulcio(X509 *leaf, const SigstoreTrust &trust, const QDateTime &at)
{
    for (const auto &authority : trust.authorities) {
        if (!covers(authority.start, authority.end, at))
            continue;
        ChainStore chain;
        if (!chain.load(authority.chainDer, at))
            continue;
        std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> ctx(X509_STORE_CTX_new(), X509_STORE_CTX_free);
        if (!ctx || X509_STORE_CTX_init(ctx.get(), chain.store.get(), leaf, chain.intermediates.get()) != 1)
            continue;
        X509_VERIFY_PARAM_set_time(X509_STORE_CTX_get0_param(ctx.get()), time_t(at.toMSecsSinceEpoch() / 1000));
        if (X509_verify_cert(ctx.get()) == 1)
            return true;
    }
    return false;
}

// What the certificate says about who signed.
VerifiedBundle identityOf(X509 *leaf)
{
    VerifiedBundle out;
    out.sourceRepository = extension(leaf, "1.3.6.1.4.1.57264.1.12", true);
    out.sourceRef = extension(leaf, "1.3.6.1.4.1.57264.1.14", true);
    out.buildSignerUri = extension(leaf, "1.3.6.1.4.1.57264.1.9", true);
    out.buildConfigUri = extension(leaf, "1.3.6.1.4.1.57264.1.18", true);
    out.issuer = extension(leaf, "1.3.6.1.4.1.57264.1.8", true);
    if (out.issuer.isEmpty())
        out.issuer = extension(leaf, "1.3.6.1.4.1.57264.1.1", false);
    out.san = sanUri(leaf);
    return out;
}

QByteArray leafCertificateDer(const QJsonObject &bundle)
{
    // v0.3 "certificate", older "x509CertificateChain".
    const QJsonObject material = bundle.value(QStringLiteral("verificationMaterial")).toObject();
    QByteArray der = fromB64(material.value(QStringLiteral("certificate")).toObject().value(QStringLiteral("rawBytes")));
    if (der.isEmpty())
        der = fromB64(material.value(QStringLiteral("x509CertificateChain")).toObject()
                          .value(QStringLiteral("certificates")).toArray().at(0).toObject()
                          .value(QStringLiteral("rawBytes")));
    return der;
}

QDateTime fromAsn1Time(const ASN1_GENERALIZEDTIME *time)
{
    struct tm tm = {};
    if (!time || ASN1_TIME_to_tm(time, &tm) != 1)
        return QDateTime();
    return QDateTime(QDate(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday), QTime(tm.tm_hour, tm.tm_min, tm.tm_sec),
                     Qt::UTC);
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
    for (const QJsonValue &v : root.value(QStringLiteral("timestampAuthorities")).toArray()) {
        const QJsonObject tsa = v.toObject();
        Authority a;
        for (const QJsonValue &c : tsa.value(QStringLiteral("certChain")).toObject().value(QStringLiteral("certificates")).toArray())
            a.chainDer << fromB64(c.toObject().value(QStringLiteral("rawBytes")));
        const QJsonObject valid = tsa.value(QStringLiteral("validFor")).toObject();
        a.start = parseTime(valid.value(QStringLiteral("start")));
        a.end = parseTime(valid.value(QStringLiteral("end")));
        if (!a.chainDer.isEmpty())
            trust.timestampAuthorities << a;
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

    const QByteArray certDer = leafCertificateDer(bundle);
    X509Ptr leaf = parseCert(certDer);
    if (!leaf)
        return fail(QStringLiteral("The bundle has no signing certificate"));

    const QByteArray payload = fromB64(envelope.value(QStringLiteral("payload")));
    const QByteArray payloadType = envelope.value(QStringLiteral("payloadType")).toString().toUtf8();
    const QJsonArray signatures = envelope.value(QStringLiteral("signatures")).toArray();
    if (payload.isEmpty() || signatures.size() != 1)
        return fail(QStringLiteral("The bundle's envelope must have a payload and one signature"));
    const QByteArray signatureB64 = signatures.first().toObject().value(QStringLiteral("sig")).toString().toLatin1();

    const QByteArray signature = QByteArray::fromBase64(signatureB64);

    // 1-2. A transparency log entry for this envelope, under a trusted key.
    bool logged = false;
    QDateTime integratedAt;
    const SigstoreTrust::Log *log = nullptr;
    QString logProblem = QStringLiteral("The bundle has no transparency log entry");
    for (const QJsonValue &v : material.value(QStringLiteral("tlogEntries")).toArray()) {
        const QJsonObject entry = v.toObject();
        const QByteArray body = fromB64(entry.value(QStringLiteral("canonicalizedBody")));
        if (!entryMatches(QJsonDocument::fromJson(body).object(), payloadType, payload, signatureB64, certDer)) {
            logProblem = QStringLiteral("The transparency log entry does not match the signature");
            continue;
        }
        const Result<QDateTime> proof = verifyTransparencyLogProof(entry, trust);
        if (!proof.ok()) {
            logProblem = proof.error.message;
            continue;
        }
        logged = true;
        integratedAt = proof.value;
        log = findLog(trust, fromB64(entry.value(QStringLiteral("logId")).toObject().value(QStringLiteral("keyId"))));
        break;
    }
    if (!logged)
        return fail(logProblem);

    // 3. Timestamps: every one given must hold. Rekor v2 entries carry no
    // time of their own, so they need at least one.
    QList<QDateTime> times;
    if (integratedAt.isValid())
        times << integratedAt;
    for (const QJsonValue &v : material.value(QStringLiteral("timestampVerificationData")).toObject()
                                   .value(QStringLiteral("rfc3161Timestamps")).toArray()) {
        const Result<QDateTime> time =
            verifySignedTimestamp(fromB64(v.toObject().value(QStringLiteral("signedTimestamp"))), signature, trust);
        if (!time.ok())
            return fail(time.error.message);
        if (!integratedAt.isValid() && log && !covers(log->start, log->end, time.value))
            return fail(QStringLiteral("The transparency log was not in use at the time of the signature"));
        times << time.value;
    }
    if (times.isEmpty())
        return fail(QStringLiteral("The bundle has no trusted time of signing"));
    QDateTime signedAt = integratedAt;
    if (!signedAt.isValid())
        for (const QDateTime &time : times)
            if (!signedAt.isValid() || time < signedAt)
                signedAt = time;

    // 4. The certificate chains to a trusted Fulcio CA at each of those times.
    for (const QDateTime &time : times)
        if (!chainsToFulcio(leaf.get(), trust, time))
            return fail(QStringLiteral("The signing certificate is not from a trusted Sigstore authority"));
    if (!(X509_get_extension_flags(leaf.get()) & EXFLAG_XKUSAGE) || !(X509_get_extended_key_usage(leaf.get()) & XKU_CODE_SIGN))
        return fail(QStringLiteral("The signing certificate is not for code signing"));

    // 5. The envelope signature.
    PKeyPtr leafKey(X509_get_pubkey(leaf.get()), EVP_PKEY_free);
    if (!verifySignature(leafKey.get(), pae(payloadType, payload), signature))
        return fail(QStringLiteral("The attestation's signature is invalid"));

    VerifiedBundle out = identityOf(leaf.get());
    out.statement = QJsonDocument::fromJson(payload).object();
    out.signedAt = signedAt;
    if (payloadType != "application/vnd.in-toto+json" || out.statement.isEmpty())
        return fail(QStringLiteral("The attestation is not an in-toto statement"));
    return Result<VerifiedBundle>::success(out);
}

Result<QDateTime> verifyTransparencyLogProof(const QJsonObject &entry, const SigstoreTrust &trust)
{
    const QByteArray keyId = fromB64(entry.value(QStringLiteral("logId")).toObject().value(QStringLiteral("keyId")));
    const SigstoreTrust::Log *log = findLog(trust, keyId);
    if (!log)
        return failTime(QStringLiteral("The transparency log is not trusted"));
    PKeyPtr logKey = parseKey(log->publicKeyDer);
    const QByteArray bodyB64 = entry.value(QStringLiteral("canonicalizedBody")).toString().toLatin1();
    const QByteArray body = QByteArray::fromBase64(bodyB64);

    // An inclusion proof, when present, must hold, up to a checkpoint the
    // log signed.
    const QJsonObject proof = entry.value(QStringLiteral("inclusionProof")).toObject();
    if (!proof.isEmpty()) {
        QList<QByteArray> hashes;
        for (const QJsonValue &h : proof.value(QStringLiteral("hashes")).toArray())
            hashes << fromB64(h);
        const qint64 size = proof.value(QStringLiteral("treeSize")).toString().toLongLong();
        const QByteArray root = fromB64(proof.value(QStringLiteral("rootHash")));
        if (!verifyInclusion(proof.value(QStringLiteral("logIndex")).toString().toLongLong(), size,
                             sha256('\x00' + body), hashes, root)
            || !verifyCheckpoint(proof.value(QStringLiteral("checkpoint")).toObject()
                                     .value(QStringLiteral("envelope")).toString(),
                                 logKey.get(), size, root))
            return failTime(QStringLiteral("The transparency log entry's inclusion proof is invalid"));
    }

    // Rekor v1 signs a promise of inclusion that also vouches for the time
    // of logging. Rekor v2 has neither: its inclusion proof is the evidence,
    // and the time comes from a timestamp authority.
    const QByteArray set = fromB64(entry.value(QStringLiteral("inclusionPromise")).toObject()
                                       .value(QStringLiteral("signedEntryTimestamp")));
    if (set.isEmpty()) {
        if (proof.isEmpty())
            return failTime(QStringLiteral("The transparency log entry has no proof of inclusion"));
        return Result<QDateTime>::success(QDateTime());
    }
    const qint64 integrated = entry.value(QStringLiteral("integratedTime")).toString().toLongLong();
    const QDateTime time = QDateTime::fromMSecsSinceEpoch(integrated * 1000, Qt::UTC);
    if (integrated <= 0 || !covers(log->start, log->end, time))
        return failTime(QStringLiteral("The transparency log entry has no valid time"));
    const QByteArray canonical = "{\"body\":\"" + bodyB64 + "\",\"integratedTime\":" + QByteArray::number(integrated)
                                 + ",\"logID\":\"" + keyId.toHex() + "\",\"logIndex\":"
                                 + QByteArray::number(entry.value(QStringLiteral("logIndex")).toString().toLongLong())
                                 + "}";
    if (!verifySignature(logKey.get(), canonical, set))
        return failTime(QStringLiteral("The transparency log entry's signature is invalid"));
    return Result<QDateTime>::success(time);
}

Result<QDateTime> verifySignedTimestamp(const QByteArray &der, const QByteArray &signature,
                                        const SigstoreTrust &trust)
{
    // A TimeStampResp, or the bare token inside one.
    const auto *p = reinterpret_cast<const unsigned char *>(der.constData());
    std::unique_ptr<TS_RESP, decltype(&TS_RESP_free)> response(d2i_TS_RESP(nullptr, &p, der.size()), TS_RESP_free);
    std::unique_ptr<PKCS7, decltype(&PKCS7_free)> ownToken(nullptr, PKCS7_free);
    std::unique_ptr<TS_TST_INFO, decltype(&TS_TST_INFO_free)> ownInfo(nullptr, TS_TST_INFO_free);
    PKCS7 *token = nullptr;
    TS_TST_INFO *info = nullptr;
    if (response) {
        const long status = ASN1_INTEGER_get(TS_STATUS_INFO_get0_status(TS_RESP_get_status_info(response.get())));
        if (status != 0 && status != 1) // granted, granted with modifications
            return failTime(QStringLiteral("The timestamp authority refused the timestamp"));
        token = TS_RESP_get_token(response.get());
        info = TS_RESP_get_tst_info(response.get());
    } else {
        p = reinterpret_cast<const unsigned char *>(der.constData());
        ownToken.reset(d2i_PKCS7(nullptr, &p, der.size()));
        if (ownToken)
            ownInfo.reset(PKCS7_to_TS_TST_INFO(ownToken.get()));
        token = ownToken.get();
        info = ownInfo.get();
    }
    const QDateTime time = info ? fromAsn1Time(TS_TST_INFO_get_time(info)) : QDateTime();
    if (!token || !time.isValid()) {
        ERR_clear_error();
        return failTime(QStringLiteral("The timestamp cannot be read"));
    }

    for (const auto &authority : trust.timestampAuthorities) {
        if (!covers(authority.start, authority.end, time))
            continue;
        // The authority's certificates are checked at the time they stamped.
        ChainStore chain;
        if (!chain.load(authority.chainDer, time))
            continue;
        std::unique_ptr<TS_VERIFY_CTX, decltype(&TS_VERIFY_CTX_free)> ctx(TS_VERIFY_CTX_new(), TS_VERIFY_CTX_free);
        BIO *data = BIO_new_mem_buf(signature.constData(), signature.size());
        if (!ctx || !data) {
            BIO_free(data);
            continue;
        }
        TS_VERIFY_CTX_set_flags(ctx.get(), TS_VFY_VERSION | TS_VFY_SIGNATURE | TS_VFY_DATA);
        // The context takes these over.
        TS_VERIFY_CTX_set_data(ctx.get(), data);
        TS_VERIFY_CTX_set_store(ctx.get(), chain.store.release());
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        TS_VERIFY_CTX_set_certs(ctx.get(), chain.intermediates.release());
#else
        TS_VERIFY_CTS_set_certs(ctx.get(), chain.intermediates.release());
#endif
        if (TS_RESP_verify_token(ctx.get(), token) == 1)
            return Result<QDateTime>::success(time);
    }
    ERR_clear_error();
    return failTime(QStringLiteral("The timestamp is not from a trusted timestamp authority"));
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

VerifiedBundle bundleClaimedIdentity(const QJsonObject &bundle)
{
    X509Ptr leaf = parseCert(leafCertificateDer(bundle));
    return leaf ? identityOf(leaf.get()) : VerifiedBundle();
}

Error checkSignerPolicy(const VerifiedBundle &bundle, const SignerPolicy &policy)
{
    const QString workflow = policy.workflow.trimmed();
    if (!workflow.isEmpty()) {
        // Both URIs end in "@<ref>".
        const QString signer = bundle.buildSignerUri.section(QLatin1Char('@'), 0, 0);
        const QString started = bundle.buildConfigUri.section(QLatin1Char('@'), 0, 0);
        bool matches;
        if (workflow.contains(QLatin1String("://"))) {
            matches = !signer.isEmpty() && canonicalRepositoryUrl(signer) == canonicalRepositoryUrl(workflow);
        } else {
            QString path = workflow;
            while (path.startsWith(QLatin1Char('/')))
                path.remove(0, 1);
            if (!path.contains(QLatin1Char('/')))
                path.prepend(QLatin1String(".github/workflows/"));
            const QString prefix = canonicalRepositoryUrl(bundle.sourceRepository) + QLatin1Char('/');
            matches = !started.isEmpty() && !bundle.sourceRepository.isEmpty()
                      && canonicalRepositoryUrl(started).compare(prefix + path, Qt::CaseInsensitive) == 0;
        }
        if (!matches)
            return Error::make(Error::Verification, QStringLiteral("The attestation was signed by %1, not by the workflow %2")
                                                        .arg(bundle.buildConfigUri.isEmpty() ? bundle.san
                                                                                              : bundle.buildConfigUri,
                                                             workflow));
    }
    const QString pattern = policy.refPattern.trimmed();
    if (!pattern.isEmpty()) {
        const QRegularExpression re(QStringLiteral("\\A(?:") + pattern + QStringLiteral(")\\z"));
        if (!re.isValid())
            return Error::make(Error::Verification, QStringLiteral("Invalid signing ref pattern: %1").arg(re.errorString()));
        if (bundle.sourceRef.isEmpty() || !re.match(bundle.sourceRef).hasMatch())
            return Error::make(Error::Verification, QStringLiteral("The attestation was signed from %1, which does not match %2")
                                                        .arg(bundle.sourceRef.isEmpty() ? QStringLiteral("an unknown ref")
                                                                                         : bundle.sourceRef,
                                                             pattern));
    }
    return Error();
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
