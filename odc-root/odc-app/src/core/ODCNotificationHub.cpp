#include "ODCNotificationHub.h"

static ODCNotificationHub* s_notifHubInstance = nullptr;

ODCNotificationHub* ODCNotificationHub::instance()
{
    if (!s_notifHubInstance) {
        s_notifHubInstance = new ODCNotificationHub();
    }
    return s_notifHubInstance;
}

ODCNotificationHub::ODCNotificationHub(QObject* parent)
    : QObject(parent)
{
}

void ODCNotificationHub::notify(const QString& eventId, const QVariantMap& data)
{
    emit notificationReceived(eventId, data);
}

void ODCNotificationHub::notifyAlert(const QString& title, const QString& message, int level)
{
    emit alertRaised(title, message, level);
}

void ODCNotificationHub::notifyStatus(const QString& source, const QString& status)
{
    emit statusChanged(source, status);
}

void ODCNotificationHub::notifyMessage(const QString& text)
{
    emit messageReceived(text);
}
