#pragma once

#include <QObject>
#include <QVariantMap>
#include <QHash>
#include <QString>
#include <functional>

class ODCCommandRegistry : public QObject
{
    Q_OBJECT

public:
    using SyncHandler = std::function<QVariantMap(const QVariantMap&)>;
    using AsyncHandler = std::function<void(const QVariantMap&,
                                             std::function<void(const QVariantMap&)>)>;

    static ODCCommandRegistry* instance();

    explicit ODCCommandRegistry(QObject* parent = nullptr);
    ~ODCCommandRegistry() override = default;

    void registerCommand(const QString& name, SyncHandler handler);
    void registerAsyncCommand(const QString& name, AsyncHandler handler);
    void unregisterCommand(const QString& name);

    QVariantMap dispatch(const QString& command, const QVariantMap& params);
    void dispatchAsync(const QString& command, const QVariantMap& params,
                       std::function<void(const QVariantMap&)> callback);

    Q_INVOKABLE bool hasCommand(const QString& name) const;
    Q_INVOKABLE QStringList registeredCommands() const;

private:
    QHash<QString, SyncHandler> m_syncHandlers;
    QHash<QString, AsyncHandler> m_asyncHandlers;
};
