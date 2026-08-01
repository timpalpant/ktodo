#include "credentialstore.h"

#include <KWallet>

#include <QDebug>
#include <QDir>
#include <QFile>
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
    return m_cached;
}

bool CredentialStore::hasToken() const
{
    return !m_cached.isEmpty();
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

void CredentialStore::setCached(const QString &token)
{
    if (m_cached != token) {
        m_cached = token;
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
    if (m_wallet->readPassword(EntryName, value) == 0 && !value.isEmpty()) {
        setCached(value);
        // The wallet is authoritative now, so no plaintext copy should remain.
        writeFallback({});
    } else if (!m_cached.isEmpty()) {
        // Migrate a token that a wallet-less run left in the file.
        m_wallet->writePassword(EntryName, m_cached);
        writeFallback({});
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
        if (m_walletUsable) {
            m_wallet->writePassword(EntryName, m_cached);
            writeFallback({});
        }
        break;
    case Pending::Clear:
        if (m_walletUsable) {
            m_wallet->removeEntry(EntryName);
        }
        writeFallback({});
        break;
    }
}

void CredentialStore::setAccessToken(const QString &token)
{
    setCached(token);

    if (m_walletUsable) {
        m_wallet->writePassword(EntryName, token);
        // Drop any file copy so a later run cannot resurrect an old token.
        writeFallback({});
        return;
    }

    if (m_resolving) {
        // The wallet may still open and should end up holding this.
        m_pending = Pending::Write;
    }
    // Persist now regardless: a token living only in memory would be lost on
    // quit if the wallet never opens. onWalletOpened() removes the file once
    // the wallet has taken it.
    writeFallback(token);
}

void CredentialStore::clear()
{
    setCached({});

    if (m_walletUsable) {
        m_wallet->removeEntry(EntryName);
    } else if (m_resolving) {
        m_pending = Pending::Clear;
    }
    // A copy may exist from a run that had no wallet.
    writeFallback({});
}

QString CredentialStore::fallbackPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/credentials");
}

QString CredentialStore::readFallback() const
{
    QFile f(fallbackPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

void CredentialStore::writeFallback(const QString &token)
{
    const QString path = fallbackPath();
    if (token.isEmpty()) {
        QFile::remove(path);
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "ktodo: cannot write credential file";
        return;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(token.toUtf8());
}
