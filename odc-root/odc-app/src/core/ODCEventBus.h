#pragma once

#include <QObject>
#include <QVariantMap>
#include <QJSValue>
#include <QJSEngine>
#include <QHash>
#include <QList>
#include <QString>

class ODCEventBus : public QObject
{
    Q_OBJECT

public:
    static ODCEventBus* instance();

    explicit ODCEventBus(QObject* parent = nullptr);
    ~ODCEventBus() override = default;

    void setJsEngine(QJSEngine* engine) { m_jsEngine = engine; }

    Q_INVOKABLE void sendCommand(const QString& command, const QVariantMap& params);
    Q_INVOKABLE void sendCommandAsync(const QString& command,
                                       const QVariantMap& params,
                                       const QJSValue& callback);

    Q_INVOKABLE void publishEvent(const QString& event, const QVariantMap& data);

    Q_INVOKABLE QString subscribe(const QString& event, const QJSValue& handler);
    Q_INVOKABLE void unsubscribe(const QString& subscriptionId);

signals:
    void eventPublished(const QString& event, const QVariantMap& data);
    void commandReceived(const QString& command, const QVariantMap& params,
                         const QJSValue& callback);

private:
    void dispatchToSubscribers(const QString& event, const QVariantMap& data);

    struct Subscription {
        QString id;
        QString event;
        QJSValue handler;
    };

    QHash<QString, QList<Subscription>> m_subscriptions;
    int m_nextSubscriptionId = 1;
    QJSEngine* m_jsEngine = nullptr;
};
