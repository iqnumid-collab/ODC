#include "ODCEventBus.h"

static ODCEventBus* s_eventBusInstance = nullptr;

ODCEventBus* ODCEventBus::instance()
{
    if (!s_eventBusInstance) {
        s_eventBusInstance = new ODCEventBus();
    }
    return s_eventBusInstance;
}

ODCEventBus::ODCEventBus(QObject* parent)
    : QObject(parent)
{
}

void ODCEventBus::sendCommand(const QString& command, const QVariantMap& params)
{
    QJSValue nullCallback;
    emit commandReceived(command, params, nullCallback);
}

void ODCEventBus::sendCommandAsync(const QString& command,
                                    const QVariantMap& params,
                                    const QJSValue& callback)
{
    emit commandReceived(command, params, callback);
}

void ODCEventBus::publishEvent(const QString& event, const QVariantMap& data)
{
    emit eventPublished(event, data);
    dispatchToSubscribers(event, data);
}

QString ODCEventBus::subscribe(const QString& event, const QJSValue& handler)
{
    if (!handler.isCallable()) {
        return {};
    }
    QString id = QString::number(m_nextSubscriptionId++);
    Subscription sub{id, event, handler};
    m_subscriptions[event].append(sub);
    return id;
}

void ODCEventBus::unsubscribe(const QString& subscriptionId)
{
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        auto& list = it.value();
        for (int i = 0; i < list.size(); ++i) {
            if (list[i].id == subscriptionId) {
                list.removeAt(i);
                if (list.isEmpty()) {
                    m_subscriptions.erase(it);
                }
                return;
            }
        }
    }
}

void ODCEventBus::dispatchToSubscribers(const QString& event, const QVariantMap& data)
{
    if (!m_jsEngine) return;

    auto it = m_subscriptions.find(event);
    if (it != m_subscriptions.end()) {
        auto list = it.value();
        for (const auto& sub : list) {
            if (sub.handler.isCallable()) {
                QJSValue cb(sub.handler);
                QJSValueList args;
                args << m_jsEngine->toScriptValue(event);
                args << m_jsEngine->toScriptValue(data);
                const_cast<QJSValue&>(cb).call(args);
            }
        }
    }
}
