#include "credentialstore.h"

#include <KWallet>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
const QString FolderName = QStringLiteral("ktodo");
const QString EntryName = QStringLiteral("access_token");
} // namespace

CredentialStore::CredentialStore(QObject *parent)
    : QObject(parent)
{}

CredentialStore::~CredentialStore()
{
    delete m_wallet;
}

QString CredentialStore::accessToken() const
{
    return m_cached.accessToken;
}

QString CredentialStore::refreshToken() const
{
    return m_cached.refreshToken;
}

QDateTime CredentialStore::accessTokenExpiresAt() const
{
    return m_cached.expiresAt;
}

bool CredentialStore::hasToken() const
{
    return !m_cached.accessToken.isEmpty();
}

bool CredentialStore::hasRefreshToken() const
{
    return !m_cached.refreshToken.isEmpty();
}

bool CredentialStore::isResolving() const
{
    return m_resolving;
}

void CredentialStore::setResolving(bool resolving)
{
    if (m_resolving != resolving) {
        m_resolving = resolving;
        Q_EMIT resolvingChanged();
    }
}

void CredentialStore::setCached(const Tokens &tokens)
{
    if (m_cached.accessToken != tokens.accessToken || m_cached.refreshToken != tokens.refreshToken || m_cached.expiresAt != tokens.expiresAt
        || m_cached.updatedAt != tokens.updatedAt) {
        m_cached = tokens;
        Q_EMIT tokenChanged();
    }
}

void CredentialStore::resolve()
{
    if (m_started) {
        return;
    }
    m_started = true;

    // The file first: one open() and no IPC, so a token left there by a run
    // that had no wallet is usable before the window is even shown.
    setCached(readFallback());

    if (!KWallet::Wallet::isEnabled()) {
        return;
    }

    // Asynchronous on purpose. The synchronous form blocks until the user
    // answers the unlock or wallet-creation prompt, and nothing has drawn yet.
    setResolving(true);
    m_wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Asynchronous);
    if (!m_wallet) {
        setResolving(false);
        return;
    }
    connect(m_wallet, &KWallet::Wallet::walletOpened, this, &CredentialStore::onWalletOpened);
}

void CredentialStore::onWalletOpened(bool ok)
{
    m_walletUsable = ok && m_wallet && m_wallet->isOpen() && (m_wallet->hasFolder(FolderName) || m_wallet->createFolder(FolderName))
                     && m_wallet->setFolder(FolderName);

    if (!m_walletUsable) {
        qWarning() << "ktodo: KWallet unavailable, using file storage";
        delete m_wallet;
        m_wallet = nullptr;
        flushPending();
        setResolving(false);
        return;
    }

    if (m_pending != Pending::None) {
        // A sign-in or sign-out beat the wallet to it; that is the newer truth.
        flushPending();
        setResolving(false);
        return;
    }

    QString value;
    const bool hasWalletValue = m_wallet->readPassword(EntryName, value) == 0 && !value.isEmpty();
    const Tokens walletTokens = hasWalletValue ? decode(value) : Tokens{};
    const bool fallbackClear = m_cached.accessToken.isEmpty() && m_cached.updatedAt.isValid();

    // A fallback is normally only present while the wallet is unavailable.
    // If a wallet write failed, however, it is the newer source of truth. The
    // timestamp in the JSON payload prevents an old wallet record from
    // resurrecting an expired token on the next launch.
    if (fallbackClear && (walletTokens.accessToken.isEmpty() || !isNewer(walletTokens, m_cached))) {
        // A clear can happen while KWallet is locked or temporarily down. Keep
        // its timestamped, credential-free fallback until we have actually
        // removed the old wallet entry; otherwise that stale token would make
        // a signed-out user look signed in again on the next launch.
        if (writeWallet({})) {
            writeFallback({});
        }
    } else if (hasToken() && (!walletTokens.accessToken.isEmpty() && !isNewer(walletTokens, m_cached))) {
        if (writeWallet(m_cached)) {
            writeFallback({});
        }
    } else if (!walletTokens.accessToken.isEmpty()) {
        setCached(walletTokens);
        // The wallet is authoritative now, so no plaintext copy should remain.
        writeFallback({});
    } else if (hasToken()) {
        // Migrate a token that a wallet-less run left in the file.
        if (writeWallet(m_cached)) {
            writeFallback({});
        }
    }

    setResolving(false);
}

void CredentialStore::flushPending()
{
    const Pending pending = m_pending;
    m_pending = Pending::None;

    switch (pending) {
    case Pending::None:
        break;
    case Pending::Write:
        if (m_walletUsable && writeWallet(m_cached)) {
            writeFallback({});
        } else {
            writeFallback(m_cached);
        }
        break;
    case Pending::Clear:
        if (m_walletUsable && writeWallet({})) {
            writeFallback({});
        } else {
            // See onWalletOpened(): retain a non-secret tombstone until the
            // wallet entry has definitely been removed.
            writeFallback(m_cached);
        }
        break;
    }
}

void CredentialStore::setTokens(const QString &accessToken, const QString &refreshToken, const QDateTime &expiresAt)
{
    Tokens tokens;
    tokens.accessToken = accessToken;
    tokens.refreshToken = refreshToken;
    tokens.expiresAt = expiresAt.isValid() ? expiresAt.toUTC() : QDateTime{};
    tokens.updatedAt = QDateTime::currentDateTimeUtc();
    setCached(tokens);

    if (m_walletUsable) {
        if (writeWallet(tokens)) {
            // Drop any file copy so a later run cannot resurrect an old token.
            writeFallback({});
        } else {
            // Do not turn a temporary KWallet write failure into a forced
            // re-login at the next launch.
            writeFallback(tokens);
        }
        return;
    }

    if (m_resolving) {
        // The wallet may still open and should end up holding this.
        m_pending = Pending::Write;
    }
    // Persist now regardless: a token living only in memory would be lost on
    // quit if the wallet never opens. onWalletOpened() removes the file once
    // the wallet has taken it.
    writeFallback(tokens);
}

void CredentialStore::clear()
{
    Tokens cleared;
    cleared.updatedAt = QDateTime::currentDateTimeUtc();
    setCached(cleared);

    if (m_walletUsable) {
        if (writeWallet({})) {
            writeFallback({});
        } else {
            writeFallback(cleared);
        }
    } else if (m_resolving) {
        m_pending = Pending::Clear;
        writeFallback(cleared);
    } else {
        // The timestamp is a tombstone, not a credential. It prevents a
        // temporarily inaccessible wallet from reviving an old session.
        writeFallback(cleared);
    }
}

QString CredentialStore::fallbackPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/credentials");
}

CredentialStore::Tokens CredentialStore::readFallback() const
{
    QFile f(fallbackPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return decode(QString::fromUtf8(f.readAll()).trimmed());
}

bool CredentialStore::writeFallback(const Tokens &tokens)
{
    const QString path = fallbackPath();
    if (tokens.accessToken.isEmpty() && !tokens.updatedAt.isValid()) {
        return QFile::remove(path) || !QFile::exists(path);
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "ktodo: cannot write credential file";
        return false;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (f.write(encode(tokens).toUtf8()) < 0 || !f.commit()) {
        qWarning() << "ktodo: cannot save credential file";
        return false;
    }
    return true;
}

bool CredentialStore::writeWallet(const Tokens &tokens)
{
    if (!m_walletUsable || !m_wallet) {
        return false;
    }

    const int result = tokens.accessToken.isEmpty() ? m_wallet->removeEntry(EntryName) : m_wallet->writePassword(EntryName, encode(tokens));
    if (result != 0) {
        qWarning() << "ktodo: could not update KWallet credentials";
        return false;
    }
    return true;
}

QString CredentialStore::encode(const Tokens &tokens)
{
    QJsonObject json{
        {QStringLiteral("access_token"), tokens.accessToken},
        {QStringLiteral("refresh_token"), tokens.refreshToken},
        {QStringLiteral("updated_at"), tokens.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
    };
    if (tokens.expiresAt.isValid()) {
        json.insert(QStringLiteral("expires_at"), tokens.expiresAt.toUTC().toString(Qt::ISODateWithMs));
    }
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

CredentialStore::Tokens CredentialStore::decode(const QString &value)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(value.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonObject json = document.object();
        Tokens tokens;
        tokens.accessToken = json.value(QStringLiteral("access_token")).toString();
        tokens.refreshToken = json.value(QStringLiteral("refresh_token")).toString();
        tokens.expiresAt = QDateTime::fromString(json.value(QStringLiteral("expires_at")).toString(), Qt::ISODateWithMs);
        if (!tokens.expiresAt.isValid()) {
            tokens.expiresAt = QDateTime::fromString(json.value(QStringLiteral("expires_at")).toString(), Qt::ISODate);
        }
        tokens.updatedAt = QDateTime::fromString(json.value(QStringLiteral("updated_at")).toString(), Qt::ISODateWithMs);
        if (!tokens.updatedAt.isValid()) {
            tokens.updatedAt = QDateTime::fromString(json.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
        }
        return tokens;
    }

    // Version 1 stored a bare access token. Retain it as a legacy credential:
    // Todoist's older OAuth applications issue long-lived access tokens and
    // therefore do not have a refresh token to recover.
    Tokens tokens;
    tokens.accessToken = value.trimmed();
    return tokens;
}

bool CredentialStore::isNewer(const Tokens &candidate, const Tokens &than)
{
    if (!candidate.updatedAt.isValid()) {
        return false;
    }
    return !than.updatedAt.isValid() || candidate.updatedAt > than.updatedAt;
}
