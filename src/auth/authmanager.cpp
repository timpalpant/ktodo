#include "authmanager.h"

#include <QQmlEngine>

#include "auth/credentialstore.h"

#include <KLocalizedString>

#include <QDebug>
#include <QDesktopServices>
#include <QHostAddress>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

#include "version.h"

namespace {

// Fixed because Todoist compares the redirect URI against the value
// registered for the client; an ephemeral port would never match.
constexpr quint16 CallbackPort = 8788;
const QString CallbackHost = QStringLiteral("127.0.0.1");
const QString CallbackPath = QStringLiteral("/callback");

const QString AuthorizeUrl = QStringLiteral("https://todoist.com/oauth/authorize");
const QString TokenUrl = QStringLiteral("https://todoist.com/oauth/access_token");

// Enough to read and write tasks and to delete projects the user owns.
const QString Scopes = QStringLiteral("data:read_write,data:delete");

/**
 * Resolves an OAuth client credential.
 *
 * Precedence is environment (handy during development via a .env file), then
 * the user's own registration in ktodorc, then whatever the build baked in.
 * That order lets a packaged build work out of the box while still letting a
 * user substitute their own Todoist app.
 */
QString credential(const QString &envKey, const QString &configKey, const char *compiledIn)
{
    const QString fromEnv = QProcessEnvironment::systemEnvironment().value(envKey);
    if (!fromEnv.isEmpty()) {
        return fromEnv;
    }

    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/ktodorc"), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("OAuth"));
    const QString fromConfig = settings.value(configKey).toString();
    if (!fromConfig.isEmpty()) {
        return fromConfig;
    }

    return QString::fromLatin1(compiledIn);
}

} // namespace

AuthManager::AuthManager(CredentialStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    m_clientId = credential(QStringLiteral("KTODO_CLIENT_ID"), QStringLiteral("ClientId"), KTODO_OAUTH_CLIENT_ID);
    m_clientSecret = credential(QStringLiteral("KTODO_CLIENT_SECRET"), QStringLiteral("ClientSecret"), KTODO_OAUTH_CLIENT_SECRET);

    if (m_store) {
        // The token is looked up in the background, so the answer can arrive
        // after the UI has already bound to `authenticated`.
        connect(m_store, &CredentialStore::tokenChanged, this, &AuthManager::authenticatedChanged);
        connect(m_store, &CredentialStore::resolvingChanged, this, &AuthManager::resolvingChanged);
    }
}

AuthManager::~AuthManager() = default;

bool AuthManager::isConfigured() const
{
    return !m_clientId.isEmpty() && !m_clientSecret.isEmpty();
}

QString AuthManager::redirectUri() const
{
    return QStringLiteral("http://%1:%2%3").arg(CallbackHost).arg(CallbackPort).arg(CallbackPath);
}

bool AuthManager::isAuthenticated() const
{
    return m_store && m_store->hasToken();
}

bool AuthManager::isResolving() const
{
    return m_store && m_store->isResolving();
}

QString AuthManager::accessToken() const
{
    return m_store ? m_store->accessToken() : QString();
}

void AuthManager::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void AuthManager::setError(const QString &error)
{
    m_lastError = error;
    Q_EMIT lastErrorChanged();
}

void AuthManager::buildFlow()
{
    if (m_flow) {
        return;
    }

    m_replyHandler = new QOAuthHttpServerReplyHandler(QHostAddress(CallbackHost), CallbackPort, this);
    m_replyHandler->setCallbackHost(CallbackHost);
    m_replyHandler->setCallbackPath(CallbackPath);
    m_replyHandler->setCallbackText(i18n("<html><body style='font-family:sans-serif;text-align:center;padding:3em'>"
                                         "<h2>Signed in to Todoist</h2>"
                                         "<p>You can close this tab and return to the application.</p>"
                                         "</body></html>"));

    m_flow = new QOAuth2AuthorizationCodeFlow(this);
    m_flow->setAuthorizationUrl(QUrl(AuthorizeUrl));
    m_flow->setTokenUrl(QUrl(TokenUrl));
    m_flow->setClientIdentifier(m_clientId);
    m_flow->setClientIdentifierSharedKey(m_clientSecret);
    // setScope() is deprecated in favour of requested scope tokens, but that
    // API joins tokens with spaces per RFC 6749 while Todoist expects a
    // comma-separated list. The literal form is the one that works here.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    m_flow->setScope(Scopes);
    QT_WARNING_POP
    m_flow->setReplyHandler(m_replyHandler);
    // PKCE (RFC 7636). A desktop app is a public client whose secret ships
    // inside the package, so an intercepted authorization code is otherwise
    // redeemable by anyone who has that package. PKCE binds the code to this
    // one sign-in attempt and makes interception useless.
    //
    // Todoist accepts the challenge parameters; whether it *enforces* the
    // verifier at token exchange is not something a client can observe, so
    // this is protection only if the server honours it. Sending it costs
    // nothing either way — unknown parameters are ignored.
    m_flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);

    connect(m_flow, &QOAuth2AuthorizationCodeFlow::authorizeWithBrowser, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });

    connect(m_flow, &QOAuth2AuthorizationCodeFlow::granted, this, [this] {
        const QString token = m_flow->token();
        setBusy(false);
        if (token.isEmpty()) {
            setError(i18n("Todoist did not return an access token."));
            return;
        }
        m_store->setAccessToken(token);
        setError({});
        Q_EMIT authenticatedChanged();
        Q_EMIT signedIn();
    });

    connect(m_flow, &QAbstractOAuth2::requestFailed, this, [this](auto error) {
        setBusy(false);
        setError(i18n("Sign-in failed (error %1). Please try again.", static_cast<int>(error)));
    });

    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this, [this](const QString &error, const QString &description, const QUrl &) {
        setBusy(false);
        setError(description.isEmpty() ? error : description);
    });
}

void AuthManager::signIn()
{
    if (!isConfigured()) {
        setError(i18n("No OAuth client credentials are configured. Set ClientId and "
                      "ClientSecret in the [OAuth] group of ktodorc."));
        return;
    }

    setError({});
    buildFlow();

    if (!m_replyHandler->isListening()) {
        setError(i18n("Could not listen on %1 for the sign-in redirect. "
                      "Another application may be using that port.",
                      redirectUri()));
        return;
    }

    setBusy(true);
    m_flow->grant();
}

void AuthManager::cancel()
{
    setBusy(false);
}

void AuthManager::signOut()
{
    if (m_store) {
        m_store->clear();
    }
    setError({});
    Q_EMIT authenticatedChanged();
    Q_EMIT signedOut();
}

void AuthManager::handleUnauthorized()
{
    // The stored token is no longer accepted; drop it so the UI returns to
    // the sign-in page rather than retrying forever.
    if (m_store) {
        m_store->clear();
    }
    setError(i18n("Your Todoist session has expired. Please sign in again."));
    Q_EMIT authenticatedChanged();
    Q_EMIT authorizationExpired();
}

// ---------------------------------------------------------------------------
// QML singleton access
//
// The instance is built in main() with its dependencies wired up; create()
// hands that same object to the QML engine. Ownership stays in C++ so the
// engine does not delete an object main() still refers to.
// ---------------------------------------------------------------------------

namespace {
AuthManager *s_instance = nullptr;
}

void AuthManager::setInstance(AuthManager *instance)
{
    s_instance = instance;
}

AuthManager *AuthManager::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine);
    Q_UNUSED(scriptEngine);
    Q_ASSERT(s_instance);
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}
