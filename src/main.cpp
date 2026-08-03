#include "application.h"
#include "api/apiclient.h"
#include "auth/authmanager.h"
#include "auth/credentialstore.h"
#include "data/database.h"
#include "data/repository.h"
#include "notifier.h"
#include "sync/syncengine.h"

#include <KAboutData>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "version.h"

int main(int argc, char *argv[])
{
    // Widgets rather than QGuiApplication: KWallet and the Breeze style
    // integration both expect a QApplication.
    QApplication app(argc, argv);

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("ktodo"));

    // Honor the user's Plasma style; fall back to Breeze elsewhere.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    KAboutData aboutData(QStringLiteral("ktodo"),
                         i18n("KTodo"),
                         QStringLiteral(KTODO_VERSION_STRING),
                         i18n("A native application for Todoist"),
                         KAboutLicense::GPL_V3,
                         i18n("© 2026 the ktodo authors"));
    // Rendered by the About page, so the disclaimer travels with the app and
    // is not something a user has to find in the README.
    aboutData.setOtherText(i18n("KTodo is an unofficial client and is not affiliated with, "
                                "endorsed by, or supported by Doist, the makers of Todoist."));
    aboutData.setHomepage(QStringLiteral("https://github.com/timpalpant/ktodo"));
    aboutData.setDesktopFileName(QStringLiteral(KTODO_APP_ID));
    aboutData.setBugAddress(QByteArrayLiteral("https://github.com/timpalpant/ktodo/issues"));
    KAboutData::setApplicationData(aboutData);

    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(KTODO_APP_ID), QIcon::fromTheme(QStringLiteral("view-task"))));

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    if (!Database::instance().open()) {
        qCritical("ktodo: could not open the local cache: %s", qUtf8Printable(Database::instance().lastError()));
        return 1;
    }

    auto *credentials = new CredentialStore(&app);
    auto *auth = new AuthManager(credentials, &app);
    // Returns immediately; the wallet opens in the background so a locked or
    // uncreated wallet cannot stall startup before the window is up.
    credentials->resolve();
    auto *api = new ApiClient(auth, &app);
    Repository *repo = Repository::instance();
    auto *sync = new SyncEngine(api, repo, auth, &app);
    auto *notifier = new Notifier(repo, &app);
    auto *application = new Application(repo, sync, auth, &app);

    // A KWallet-backed credential arrives after startup. Track the actual
    // authenticated state rather than only the interactive sign-in signal, or
    // a persisted session reaches the Today page without ever starting sync.
    bool syncEnabled = false;
    const auto updateSyncState = [auth, sync, notifier, &syncEnabled] {
        const bool shouldSync = auth->isAuthenticated();
        if (shouldSync == syncEnabled) {
            return;
        }
        syncEnabled = shouldSync;
        if (shouldSync) {
            sync->start();
            notifier->start();
        } else {
            sync->stop();
            notifier->stop();
        }
    };
    QObject::connect(auth, &AuthManager::authenticatedChanged, &app, updateSyncState);
    // A newly authorized user may be different from the previous one, so do
    // not reuse an incremental sync token from the old local cache.
    QObject::connect(auth, &AuthManager::signedIn, sync, [sync] { sync->resync(); });

    // Registered as typed QML singletons rather than context properties, so
    // the QML tooling can resolve every use of App, Auth and Sync.
    Application::setInstance(application);
    AuthManager::setInstance(auth);
    SyncEngine::setInstance(sync);

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("AboutData"), QVariant::fromValue(aboutData));

    engine.loadFromModule("io.github.timpalpant.ktodo", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCritical("ktodo: failed to load the QML interface");
        return 1;
    }

    updateSyncState();

    return app.exec();
}
