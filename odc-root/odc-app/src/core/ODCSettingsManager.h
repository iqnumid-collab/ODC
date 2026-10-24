#pragma once

#include <QObject>
#include <QVariant>
#include <QSettings>
#include <QStringList>

class ODCSettingsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList groupNames READ groupNames NOTIFY groupsChanged)

public:
    explicit ODCSettingsManager(QObject* parent = nullptr);
    ~ODCSettingsManager() override = default;

    Q_INVOKABLE QVariant value(const QString& group, const QString& key,
                                const QVariant& defaultValue = QVariant());
    Q_INVOKABLE void setValue(const QString& group, const QString& key,
                               const QVariant& value);
    Q_INVOKABLE QStringList keys(const QString& group);
    Q_INVOKABLE void remove(const QString& group, const QString& key);
    Q_INVOKABLE void clearGroup(const QString& group);
    Q_INVOKABLE void sync();

    QStringList groupNames() const;
    bool contains(const QString& group, const QString& key);

signals:
    void valueChanged(const QString& group, const QString& key,
                       const QVariant& value);
    void groupsChanged();

private:
    QSettings m_settings;
};
