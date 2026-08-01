#pragma once

#include <QObject>
#include <QString>

namespace KWallet {
class Wallet;
}

/**
 * Stores the OAuth access token in KWallet, falling back to a restricted file
 * under the user's data directory when no wallet is available (a headless
 * session, or a desktop without kwalletd running).
 *
 * Resolution is asynchronous and must stay that way. accessToken() backs a
 * QML-bound property, so it is read during binding evaluation while the engine
 * is loading; opening a wallet is a blocking call that can sit indefinitely
 * behind an unlock or wallet-creation prompt, which deadlocks startup before
 * the window ever appears.
 *
 * So: the file is consulted immediately (a cheap read, no IPC) and the wallet
 * is opened in the background. accessToken() returns what is known so far and
 * never blocks; tokenChanged() fires if the wallet later supplies a different
 * answer, and resolvingChanged() fires once the outcome is settled.
 *
 * The token is never written to KConfig, which is world-readable.
 */
class CredentialStore : public QObject
{
    Q_OBJECT

public:
    explicit CredentialStore(QObject *parent = nullptr);
    ~CredentialStore() override;

    /// Begins resolution. Returns immediately; safe to call more than once.
    void resolve();

    /// Never blocks. Empty until resolution finds a token.
    QString accessToken() const;
    bool hasToken() const;

    /// True while the wallet is still opening and the answer may yet change.
    bool isResolving() const;

    void setAccessToken(const QString &token);
    void clear();

Q_SIGNALS:
    void tokenChanged();
    void resolvingChanged();

private:
    void onWalletOpened(bool ok);
    /// Applies a write or clear that arrived while the wallet was opening.
    void flushPending();
    void setResolving(bool resolving);
    void setCached(const QString &token);

    QString fallbackPath() const;
    QString readFallback() const;
    void writeFallback(const QString &token);

    KWallet::Wallet *m_wallet = nullptr;
    bool m_walletUsable = false;
    bool m_resolving = false;
    bool m_started = false;

    /// A write or clear issued before the wallet finished opening.
    enum class Pending { None, Write, Clear };
    Pending m_pending = Pending::None;

    QString m_cached;
};
