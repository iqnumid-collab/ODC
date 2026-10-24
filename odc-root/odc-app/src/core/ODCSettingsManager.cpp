#include "ODCSettingsManager.h"

ODCSettingsManager::ODCSettingsManager(QObject* parent)
    : QObject(parent)
{
}

QVariant ODCSettingsManager::value(const QString& group, const QString& key,
                                    const QVariant& defaultValue)
{
    m_settings.beginGroup(group);
    QVariant v = m_settings.value(key, defaultValue);
    m_settings.endGroup();
    return v;
}

void ODCSettingsManager::setValue(const QString& group, const QString& key,
                                   const QVariant& value)
{
    m_settings.beginGroup(group);
    QVariant oldValue = m_settings.value(key);
    if (oldValue != value) {
        m_settings.setValue(key, value);
        m_settings.endGroup();
        emit valueChanged(group, key, value);
        emit groupsChanged();
    } else {
        m_settings.endGroup();
    }
}

QStringList ODCSettingsManager::keys(const QString& group)
{
    m_settings.beginGroup(group);
    QStringList result = m_settings.childKeys();
    m_settings.endGroup();
    return result;
}

void ODCSettingsManager::remove(const QString& group, const QString& key)
{
    m_settings.beginGroup(group);
    m_settings.remove(key);
    m_settings.endGroup();
    emit valueChanged(group, key, QVariant());
    emit groupsChanged();
}

void ODCSettingsManager::clearGroup(const QString& group)
{
    m_settings.beginGroup(group);
    QStringList allKeys = m_settings.allKeys();
    m_settings.remove(QString());
    m_settings.endGroup();
    for (const auto& key : allKeys) {
        emit valueChanged(group, key, QVariant());
    }
    emit groupsChanged();
}

void ODCSettingsManager::sync()
{
    m_settings.sync();
}

QStringList ODCSettingsManager::groupNames() const
{
    return m_settings.childGroups();
}

bool ODCSettingsManager::contains(const QString& group, const QString& key)
{
    m_settings.beginGroup(group);
    bool result = m_settings.contains(key);
    m_settings.endGroup();
    return result;
}
