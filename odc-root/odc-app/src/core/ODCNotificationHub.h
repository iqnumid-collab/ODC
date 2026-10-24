#pragma once

#include <QObject>
#include <QVariantMap>

class ODCNotificationHub : public QObject
{
    Q_OBJECT

public:
    static ODCNotificationHub* instance();

    explicit ODCNotificationHub(QObject* parent = nullptr);
    ~ODCNotificationHub() override = default;

    Q_INVOKABLE void notify(const QString& eventId, const QVariantMap& data);
    Q_INVOKABLE void notifyAlert(const QString& title, const QString& message,
                                  int level = 0);
    Q_INVOKABLE void notifyStatus(const QString& source, const QString& status);
    Q_INVOKABLE void notifyMessage(const QString& text);

signals:
    void notificationReceived(const QString& eventId, const QVariantMap& data);
    void alertRaised(const QString& title, const QString& message, int level);
    void statusChanged(const QString& source, const QString& status);
    void messageReceived(const QString& text);
};
