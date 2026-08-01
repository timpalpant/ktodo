#pragma once

#include <QObject>
#include <QString>
#include <qqmlintegration.h>

class CredentialStore;
class QJSEngine;
class QQmlEngine;
class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;

/**
 * Drives Todoist's OAuth2 authorization-code flow.
 *
 * The redirect lands on a loopback listener, which is the standard native-app
 * pattern. Todoist matches the redirect URI exactly against the one registered
 * for the client, so the port is fixed rather than ephemeral.
 */
class AuthManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Auth)
    QML_SINGLETON

    Q_PROPERTY(bool authenticated READ isAuthenticated NOTIFY authenticatedChanged)
    /// True while the stored token is still being looked up; `authenticated`
    /// is not yet trustworthy and the UI should wait rather than ask to log in.
    Q_PROPERTY(bool resolving READ isResolving NOTIFY resolvingChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool configured READ isConfigured CONSTANT)
    Q_PROPERTY(QString redirectUri READ redirectUri CONSTANT)

public:
    explicit AuthManager(CredentialStore *store, QObject *parent = nullptr);
    ~AuthManager() override;

    static void setInstance(AuthManager *instance);
    static AuthManager *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    bool isAuthenticated() const;
    bool isResolving() const;
    bool isBusy() const { return m_busy; }
    QString lastError() const { return m_lastError; }
    /// False when no client id/secret was supplied, so sign-in cannot start.
    bool isConfigured() const;
    QString redirectUri() const;

    QString accessToken() const;

    /// Opens the system browser to begin sign-in.
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void authenticatedChanged();
    void resolvingChanged();
    void busyChanged();
    void lastErrorChanged();
    void signedIn();
    void signedOut();
    /// Emitted when the server rejects our token, so the UI can prompt again.
    void authorizationExpired();

public Q_SLOTS:
    void handleUnauthorized();

private:
    void setBusy(bool busy);
    void setError(const QString &error);
    void buildFlow();

    CredentialStore *m_store = nullptr;
    QOAuth2AuthorizationCodeFlow *m_flow = nullptr;
    QOAuthHttpServerReplyHandler *m_replyHandler = nullptr;

    QString m_clientId;
    QString m_clientSecret;
    QString m_lastError;
    bool m_busy = false;
};
