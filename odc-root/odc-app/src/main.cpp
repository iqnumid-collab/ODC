#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "app/ODCAppEngine.h"
#include "core/ODCEventBus.h"
#include "core/ODCCommandRegistry.h"
#include "core/ODCNotificationHub.h"
#include "core/ODCSettingsManager.h"
#include "ui/ODCTheme.h"
#include "ui/ODCScreenToolsController.h"

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QGuiApplication app(argc, argv);
    app.setApplicationName("OpenDroneCore");
    app.setOrganizationName("OpenDroneCore");
    app.setApplicationVersion("1.0.0");

    QQuickStyle::setStyle("Basic");

    auto* appEngine  = new ODCAppEngine(&app);
    auto* theme       = appEngine->theme();
    auto* settings    = appEngine->settingsManager();
    auto* eventBus    = ODCEventBus::instance();
    auto* cmdRegistry = ODCCommandRegistry::instance();
    auto* notifHub    = ODCNotificationHub::instance();
    auto* screenTools = new ODCScreenToolsController(&app);

    QQmlApplicationEngine qmlEngine;
    qmlEngine.addImportPath("qrc:/qml");
    eventBus->setJsEngine(&qmlEngine);
    appEngine->setQmlEngine(&qmlEngine);

    QObject::connect(eventBus, &ODCEventBus::commandReceived,
        [cmdRegistry, &qmlEngine](const QString& cmd, const QVariantMap& params,
                                   const QJSValue& callback) {
            if (callback.isCallable()) {
                cmdRegistry->dispatchAsync(cmd, params,
                    [callback, &qmlEngine](const QVariantMap& result) mutable {
                        if (!callback.isCallable()) return;
                        QJSValue cb(callback);
                        QJSValueList args;
                        args << qmlEngine.toScriptValue(result);
                        const_cast<QJSValue&>(cb).call(args);
                    });
            } else {
                cmdRegistry->dispatch(cmd, params);
            }
        });

    QObject::connect(notifHub, &ODCNotificationHub::alertRaised,
        [eventBus](const QString& title, const QString& message, int level) {
            QVariantMap data;
            data["title"] = title;
            data["message"] = message;
            data["level"] = level;
            eventBus->publishEvent("alert", data);
        });

    QObject::connect(notifHub, &ODCNotificationHub::statusChanged,
        [eventBus](const QString& source, const QString& status) {
            QVariantMap data;
            data["source"] = source;
            data["status"] = status;
            eventBus->publishEvent("statusChanged", data);
        });

    QObject::connect(appEngine, &ODCAppEngine::messageDialogRequested,
        [eventBus](const QString& title, const QString& message) {
            QVariantMap data;
            data["title"] = title;
            data["message"] = message;
            eventBus->publishEvent("messageDialog", data);
        });

    auto* rootCtx = qmlEngine.rootContext();
    rootCtx->setContextProperty("ODCApp", appEngine);
    rootCtx->setContextProperty("ODCEventBus", eventBus);
    rootCtx->setContextProperty("ODCNotificationHub", notifHub);
    rootCtx->setContextProperty("ODCTheme", theme);
    rootCtx->setContextProperty("ODCSettings", settings);
    rootCtx->setContextProperty("ODCScreenToolsController", screenTools);

    qmlEngine.load(QUrl("qrc:/qml/OpenDroneCore/qml/OpenDroneCore/MainWindow.qml"));

    if (qmlEngine.rootObjects().isEmpty()) {
        qCritical() << "Failed to load QML entry point";
        return -1;
    }

    appEngine->initialize();

    return app.exec();
}
