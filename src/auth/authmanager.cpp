#include "authmanager.h"

#include <QQmlEngine>

#include "auth/credentialstore.h"

#include <KLocalizedString>

#include <QDebug>
#include <QDesktopServices>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

#include <limits>

#include "version.h"

namespace {

// Fixed because Todoist compares the redirect URI against the value
// registered for the client; an ephemeral port would never match.
constexpr quint16 CallbackPort = 8788;
const QString CallbackHost = QStringLiteral("127.0.0.1");
const QString CallbackPath = QStringLiteral("/callback");

const QString AuthorizeUrl = QStringLiteral("https://app.todoist.com/oauth/authorize");
const QString TokenUrl = QStringLiteral("https://api.todoist.com/oauth/access_token");

// Enough to read and write tasks and to delete projects the user owns.
const QString Scopes = QStringLiteral("data:read_write,data:delete");
constexpr qint64 RefreshLeadTimeSeconds = 120;
constexpr int MaxRefreshRetryDelayMs = 5 * 60 * 1000;

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
    , m_network(new QNetworkAccessManager(this))
    , m_refreshTimer(new QTimer(this))
{
    m_clientId = credential(QStringLiteral("KTODO_CLIENT_ID"), QStringLiteral("ClientId"), KTODO_OAUTH_CLIENT_ID);
    m_clientSecret = credential(QStringLiteral("KTODO_CLIENT_SECRET"), QStringLiteral("ClientSecret"), KTODO_OAUTH_CLIENT_SECRET);

    if (m_store) {
        // The token is looked up in the background, so the answer can arrive
        // after the UI has already bound to `authenticated`.
        connect(m_store, &CredentialStore::tokenChanged, this, [this] {
            scheduleRefresh();
            Q_EMIT authenticatedChanged();
        });
        connect(m_store, &CredentialStore::resolvingChanged, this, &AuthManager::resolvingChanged);
    }

    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, [this] { refreshAccessToken(); });
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

void AuthManager::scheduleRefresh()
{
    if (!m_refreshTimer) {
        return;
    }

    m_refreshTimer->stop();
    if (!m_store || !m_store->hasRefreshToken()) {
        return;
    }

    const QDateTime expiry = m_store->accessTokenExpiresAt();
    if (!expiry.isValid()) {
        // A provider may omit expires_in. In that case there is no safe point
        // for a proactive refresh; a 401 will still trigger one and retry the
        // affected request.
        return;
    }

    const qint64 secondsUntilRefresh = QDateTime::currentDateTimeUtc().secsTo(expiry) - RefreshLeadTimeSeconds;
    const qint64 milliseconds = qMax<qint64>(0, secondsUntilRefresh * 1000);
    m_refreshTimer->start(static_cast<int>(qMin(milliseconds, qint64(std::numeric_limits<int>::max()))));
}

void AuthManager::scheduleRefreshRetry()
{
    if (!m_refreshTimer || !m_store || !m_store->hasRefreshToken()) {
        return;
    }

    const int exponent = qMin(m_refreshFailures, 6);
    const int delay = qMin(MaxRefreshRetryDelayMs, 5000 * (1 << exponent));
    m_refreshTimer->start(delay);
}

void AuthManager::finishRefresh(RefreshResult result, const QString &error)
{
    m_refreshing = false;
    const QVector<RefreshCallback> callbacks = std::move(m_refreshCallbacks);
    m_refreshCallbacks.clear();

    for (const RefreshCallback &callback : callbacks) {
        if (callback) {
            callback(result, error);
        }
    }
}

void AuthManager::refreshAccessToken(RefreshCallback callback)
{
    if (!m_store || !m_store->hasRefreshToken()) {
        if (callback) {
            callback(RefreshResult::NoRefreshToken, i18n("No refresh token is available."));
        }
        return;
    }
    if (!isConfigured()) {
        if (callback) {
            callback(RefreshResult::PermanentFailure, i18n("OAuth client credentials are not configured."));
        }
        return;
    }

    if (callback) {
        m_refreshCallbacks.append(std::move(callback));
    }
    if (m_refreshing) {
        return;
    }
    m_refreshing = true;
    m_refreshTimer->stop();

    const QString refreshToken = m_store->refreshToken();
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), m_clientId);
    form.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), refreshToken);

    QNetworkRequest request{QUrl(TokenUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("User-Agent", "ktodo/" KTODO_VERSION_STRING " (KDE)");

    QNetworkReply *reply = m_network->post(request, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, refreshToken] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QJsonObject json = QJsonDocument::fromJson(body).object();
        const QString oauthError = json.value(QStringLiteral("error")).toString();
        const QString description = json.value(QStringLiteral("error_description")).toString();
        const QString error = description.isEmpty() ? oauthError : description;

        // Signing out or completing a fresh authorization while this request
        // was in flight makes its response stale. Never overwrite newer
        // credentials with a response for the old, rotating refresh token.
        if (!m_store || m_store->refreshToken() != refreshToken) {
            finishRefresh(RefreshResult::NoRefreshToken, i18n("The saved session changed while refreshing."));
            return;
        }

        const QString newAccessToken = json.value(QStringLiteral("access_token")).toString();
        if (status >= 200 && status < 300 && !newAccessToken.isEmpty()) {
            // Todoist rotates this value on each refresh. A grace-window retry
            // can intentionally omit it, in which case retaining the saved
            // replacement is the only safe thing to do.
            const QString newRefreshToken = json.value(QStringLiteral("refresh_token")).toString();

            QDateTime expiresAt;
            const QJsonValue expiresIn = json.value(QStringLiteral("expires_in"));
            bool validLifetime = false;
            qint64 lifetimeSeconds = 0;
            if (expiresIn.isDouble()) {
                lifetimeSeconds = qRound64(expiresIn.toDouble());
                validLifetime = lifetimeSeconds > 0;
            } else if (expiresIn.isString()) {
                lifetimeSeconds = expiresIn.toString().toLongLong(&validLifetime);
                validLifetime = validLifetime && lifetimeSeconds > 0;
            }
            if (validLifetime) {
                expiresAt = QDateTime::currentDateTimeUtc().addSecs(lifetimeSeconds);
            }

            m_store->setTokens(newAccessToken, newRefreshToken.isEmpty() ? refreshToken : newRefreshToken, expiresAt);
            m_refreshFailures = 0;
            setError({});
            scheduleRefresh();
            finishRefresh(RefreshResult::Refreshed);
            return;
        }

        const bool transient = status == 0 || status == 429 || status >= 500;
        if (transient) {
            ++m_refreshFailures;
            scheduleRefreshRetry();
            finishRefresh(RefreshResult::TransientFailure, error.isEmpty() ? reply->errorString() : error);
            return;
        }

        finishRefresh(RefreshResult::PermanentFailure, error.isEmpty() ? reply->errorString() : error);
    });
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // On older Qt versions, the constructor's loopback address already
    // determines the callback host. Qt 6.9 added this explicit override.
    m_replyHandler->setCallbackHost(CallbackHost);
#endif
    m_replyHandler->setCallbackPath(CallbackPath);
    m_replyHandler->setCallbackText(i18n("<html><body style='font-family:sans-serif;text-align:center;padding:3em'>"
                                         "<h2>Signed in to Todoist</h2>"
                                         "<p>You can close this tab and return to the application.</p>"
                                         "</body></html>"));

    m_flow = new QOAuth2AuthorizationCodeFlow(this);
    m_flow->setAuthorizationUrl(QUrl(AuthorizeUrl));
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    m_flow->setTokenUrl(QUrl(TokenUrl));
#else
    m_flow->setAccessTokenUrl(QUrl(TokenUrl));
#endif
    m_flow->setClientIdentifier(m_clientId);
    m_flow->setClientIdentifierSharedKey(m_clientSecret);
    // setScope() is deprecated in favor of requested scope tokens, but that
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
        // New Todoist OAuth applications issue a one-hour access token and a
        // rotating refresh token. Qt exposes both after the code exchange;
        // keep the full set together so a restart can refresh without sending
        // the user through the browser again.
        m_store->setTokens(token, m_flow->refreshToken(), m_flow->expirationAt());
        m_refreshFailures = 0;
        scheduleRefresh();
        setError({});
        Q_EMIT authenticatedChanged();
        Q_EMIT signedIn();
    });

    connect(m_flow, &QAbstractOAuth2::requestFailed, this, [this](auto error) {
        setBusy(false);
        setError(i18n("Sign-in failed (error %1). Please try again.", static_cast<int>(error)));
    });

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    connect(m_flow, &QAbstractOAuth2::serverReportedErrorOccurred, this, [this](const QString &error, const QString &description, const QUrl &) {
#else
    connect(m_flow, &QAbstractOAuth2::error, this, [this](const QString &error, const QString &description, const QUrl &) {
#endif
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
    if (m_refreshTimer) {
        m_refreshTimer->stop();
    }
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
    if (m_refreshTimer) {
        m_refreshTimer->stop();
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
