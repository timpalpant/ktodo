#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class AuthManager;
class QNetworkAccessManager;

/**
 * Thin transport over Todoist's unified API v1.
 *
 * Only the /sync endpoint is used: it carries both incremental reads and
 * batched writes, which is what makes offline-first behaviour possible.
 * (The former sync/v9 and rest/v2 endpoints now return HTTP 410.)
 */
class ApiClient : public QObject
{
    Q_OBJECT

public:
    struct Result {
        bool ok = false;
        bool unauthorized = false; ///< Token rejected; re-authentication needed.
        bool retryable = false;    ///< Transient: offline, timeout, 5xx, 429.
        int httpStatus = 0;
        int retryAfterSeconds = 0;
        QJsonObject payload;
        QString error;
    };

    using Callback = std::function<void(const Result &)>;

    explicit ApiClient(AuthManager *auth, QObject *parent = nullptr);

    /**
     * Posts one sync request.
     *
     * @param syncToken "*" for a full sync, otherwise the last token.
     * @param resourceTypes Resources to read back; empty means all.
     * @param commands Mutations to apply, already ordered.
     */
    void sync(const QString &syncToken, const QStringList &resourceTypes, const QJsonArray &commands, Callback callback);

    /// Server-side natural-language task creation ("quick add").
    void quickAdd(const QString &text, Callback callback);

private:
    void post(const QString &path, const QByteArray &body, Callback callback);
    void postWithToken(const QString &path, const QByteArray &body, bool retriedAfterRefresh, Callback callback);

    AuthManager *m_auth = nullptr;
    QNetworkAccessManager *m_network = nullptr;
};
