#pragma once

#include <QObject>
#include <QStringList>
#include <memory>

#include "core/ODCSettingsManager.h"
#include "ui/ODCTheme.h"

class ODCEventBus;
class ODCCommandRegistry;
class ODCNotificationHub;
class ODCScreenToolsController;
class QQmlApplicationEngine;
class ODCPluginInterface;

class ODCAppEngine : public QObject
{
    Q_OBJECT

    Q_PROPERTY(ODCSettingsManager* settingsManager READ settingsManager CONSTANT)
    Q_PROPERTY(ODCTheme* theme READ theme CONSTANT)
    Q_PROPERTY(bool initialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(int zOrderTopMost READ zOrderTopMost CONSTANT)
    Q_PROPERTY(QStringList pluginList READ pluginList NOTIFY pluginListChanged)

public:
    explicit ODCAppEngine(QObject* parent = nullptr);
    ~ODCAppEngine() override = default;

    static ODCAppEngine* instance();

    ODCSettingsManager* settingsManager() const { return m_settingsManager.get(); }
    ODCTheme* theme() const { return m_theme.get(); }
    bool isInitialized() const { return m_initialized; }
    int zOrderTopMost() const { return 1000; }
    QStringList pluginList() const;

    Q_INVOKABLE void initialize();
    Q_INVOKABLE void shutdown();

    Q_INVOKABLE void showMessageDialog(const QString& title, const QString& message);

    Q_INVOKABLE void loadPlugin(const QString& pluginPath);
    Q_INVOKABLE QStringList availablePlugins() const;

    void setQmlEngine(QQmlApplicationEngine* engine) { m_qmlEngine = engine; }

signals:
    void initializedChanged(bool initialized);
    void pluginListChanged();
    void messageDialogRequested(const QString& title, const QString& message);
    void shutdownRequested();

private:
    void registerBuiltinCommands();
    void createSingletons();

    std::unique_ptr<ODCSettingsManager> m_settingsManager;
    std::unique_ptr<ODCTheme> m_theme;
    QQmlApplicationEngine* m_qmlEngine = nullptr;
    bool m_initialized = false;

    struct LoadedPlugin {
        ODCPluginInterface* instance = nullptr;
        QString path;
    };
    QList<LoadedPlugin> m_plugins;
};
