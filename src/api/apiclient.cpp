#include "apiclient.h"

#include "auth/authmanager.h"

#include <KLocalizedString>

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

#include "version.h"

namespace {
const QString ApiBase = QStringLiteral("https://api.todoist.com/api/v1");
constexpr int RequestTimeoutMs = 30000;
} // namespace

ApiClient::ApiClient(AuthManager *auth, QObject *parent)
    : QObject(parent)
    , m_auth(auth)
    , m_network(new QNetworkAccessManager(this))
{
    m_network->setTransferTimeout(RequestTimeoutMs);
}

void ApiClient::sync(const QString &syncToken, const QStringList &resourceTypes, const QJsonArray &commands, Callback callback)
{
    QJsonArray types;
    if (resourceTypes.isEmpty()) {
        types.append(QStringLiteral("all"));
    } else {
        for (const QString &t : resourceTypes) {
            types.append(t);
        }
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("sync_token"), syncToken);
    form.addQueryItem(QStringLiteral("resource_types"), QString::fromUtf8(QJsonDocument(types).toJson(QJsonDocument::Compact)));
    if (!commands.isEmpty()) {
        form.addQueryItem(QStringLiteral("commands"), QString::fromUtf8(QJsonDocument(commands).toJson(QJsonDocument::Compact)));
    }

    post(QStringLiteral("/sync"), form.toString(QUrl::FullyEncoded).toUtf8(), std::move(callback));
}

void ApiClient::completedTasks(
    const QDateTime &since, const QDateTime &until, const QString &projectId, const QString &cursor, int limit, Callback callback)
{
    QUrlQuery params;
    // Both bounds are required; the server rejects an open-ended window.
    params.addQueryItem(QStringLiteral("since"), since.toUTC().toString(Qt::ISODate));
    params.addQueryItem(QStringLiteral("until"), until.toUTC().toString(Qt::ISODate));
    params.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    if (!projectId.isEmpty()) {
        params.addQueryItem(QStringLiteral("project_id"), projectId);
    }
    if (!cursor.isEmpty()) {
        params.addQueryItem(QStringLiteral("cursor"), cursor);
    }

    get(QStringLiteral("/tasks/completed/by_completion_date?") + params.toString(QUrl::FullyEncoded), std::move(callback));
}

void ApiClient::quickAdd(const QString &text, Callback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("text"), text);
    post(QStringLiteral("/tasks/quick"), form.toString(QUrl::FullyEncoded).toUtf8(), std::move(callback));
}

void ApiClient::get(const QString &pathWithQuery, Callback callback)
{
    send(Method::Get, pathWithQuery, {}, false, std::move(callback));
}

void ApiClient::post(const QString &path, const QByteArray &body, Callback callback)
{
    send(Method::Post, path, body, false, std::move(callback));
}

void ApiClient::send(Method method, const QString &path, const QByteArray &body, bool retriedAfterRefresh, Callback callback)
{
    const QString token = m_auth ? m_auth->accessToken() : QString();
    if (token.isEmpty()) {
        Result r;
        r.unauthorized = true;
        r.error = i18n("Not signed in.");
        callback(r);
        return;
    }

    QNetworkRequest request(QUrl(ApiBase + path));
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    request.setRawHeader("User-Agent", "ktodo/" KTODO_VERSION_STRING " (KDE)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = nullptr;
    if (method == Method::Post) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        reply = m_network->post(request, body);
    } else {
        reply = m_network->get(request);
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, method, path, body, retriedAfterRefresh, cb = std::move(callback)]() mutable {
        reply->deleteLater();

        Result result;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        const QByteArray data = reply->readAll();

        if (result.httpStatus == 401 && !retriedAfterRefresh && m_auth) {
            // Modern Todoist OAuth access tokens live for one hour. A 401 is
            // the server's signal to refresh, then replay this one request
            // with the replacement token; it is not grounds to discard a
            // perfectly recoverable session.
            m_auth->refreshAccessToken(
                [this, method, path, body, cb = std::move(cb)](AuthManager::RefreshResult refresh, const QString &error) mutable {
                    if (refresh == AuthManager::RefreshResult::Refreshed) {
                        send(method, path, body, true, std::move(cb));
                        return;
                    }

                    Result refreshedResult;
                    refreshedResult.httpStatus = 401;
                    refreshedResult.error = error;
                    refreshedResult.retryable = refresh == AuthManager::RefreshResult::TransientFailure;
                    refreshedResult.unauthorized = !refreshedResult.retryable;
                    cb(refreshedResult);
                });
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            result.error = reply->errorString();

            switch (result.httpStatus) {
            case 401:
                // The token itself was rejected.
                result.unauthorized = true;
                break;
            case 403:
                // About the request, not the token: a command touching a
                // project the user cannot write to, for instance.
                break;
            case 429: {
                result.retryable = true;
                const QByteArray retryAfter = reply->rawHeader("Retry-After");
                result.retryAfterSeconds = retryAfter.isEmpty() ? 30 : retryAfter.toInt();
                break;
            }
            default:
                // No status at all means the request never reached the server.
                result.retryable = result.httpStatus == 0 || result.httpStatus >= 500;
                break;
            }

            // Todoist returns a JSON body with a human-readable error even on
            // failure; prefer it to Qt's generic message.
            const QJsonObject obj = QJsonDocument::fromJson(data).object();
            const QString apiError = obj.value(QStringLiteral("error")).toString();
            if (!apiError.isEmpty()) {
                result.error = apiError;
            }
            cb(result);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            result.error = i18n("Unexpected response from Todoist: %1", parseError.errorString());
            result.retryable = true;
            cb(result);
            return;
        }

        result.ok = true;
        result.payload = doc.object();
        cb(result);
    });
}
