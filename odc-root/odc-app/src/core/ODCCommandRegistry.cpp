#include "ODCCommandRegistry.h"

static ODCCommandRegistry* s_cmdRegistryInstance = nullptr;

ODCCommandRegistry* ODCCommandRegistry::instance()
{
    if (!s_cmdRegistryInstance) {
        s_cmdRegistryInstance = new ODCCommandRegistry();
    }
    return s_cmdRegistryInstance;
}

ODCCommandRegistry::ODCCommandRegistry(QObject* parent)
    : QObject(parent)
{
}

void ODCCommandRegistry::registerCommand(const QString& name, SyncHandler handler)
{
    m_syncHandlers[name] = std::move(handler);
}

void ODCCommandRegistry::registerAsyncCommand(const QString& name, AsyncHandler handler)
{
    m_asyncHandlers[name] = std::move(handler);
}

void ODCCommandRegistry::unregisterCommand(const QString& name)
{
    m_syncHandlers.remove(name);
    m_asyncHandlers.remove(name);
}

QVariantMap ODCCommandRegistry::dispatch(const QString& command, const QVariantMap& params)
{
    auto syncIt = m_syncHandlers.find(command);
    if (syncIt != m_syncHandlers.end()) {
        return syncIt.value()(params);
    }

    auto asyncIt = m_asyncHandlers.find(command);
    if (asyncIt != m_asyncHandlers.end()) {
        return QVariantMap{{"error", "Command requires async dispatch"},
                           {"command", command}};
    }

    return QVariantMap{{"error", "Unknown command"},
                       {"command", command}};
}

void ODCCommandRegistry::dispatchAsync(const QString& command,
                                        const QVariantMap& params,
                                        std::function<void(const QVariantMap&)> callback)
{
    auto asyncIt = m_asyncHandlers.find(command);
    if (asyncIt != m_asyncHandlers.end()) {
        asyncIt.value()(params, std::move(callback));
        return;
    }

    auto syncIt = m_syncHandlers.find(command);
    if (syncIt != m_syncHandlers.end()) {
        if (callback) {
            callback(syncIt.value()(params));
        }
        return;
    }

    if (callback) {
        callback(QVariantMap{{"error", "Unknown command"}, {"command", command}});
    }
}

bool ODCCommandRegistry::hasCommand(const QString& name) const
{
    return m_syncHandlers.contains(name) || m_asyncHandlers.contains(name);
}

QStringList ODCCommandRegistry::registeredCommands() const
{
    QStringList cmds;
    for (auto it = m_syncHandlers.keyBegin(); it != m_syncHandlers.keyEnd(); ++it) {
        cmds.append(*it);
    }
    for (auto it = m_asyncHandlers.keyBegin(); it != m_asyncHandlers.keyEnd(); ++it) {
        if (!cmds.contains(*it)) {
            cmds.append(*it);
        }
    }
    cmds.sort();
    return cmds;
}
