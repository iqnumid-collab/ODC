#include "app/ODCAppEngine.h"
#include "core/ODCEventBus.h"
#include "core/ODCCommandRegistry.h"
#include "core/ODCNotificationHub.h"
#include "core/ODCPluginInterface.h"
#include "ui/ODCPalette.h"

#include <QPluginLoader>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

static ODCAppEngine* s_appEngineInstance = nullptr;

ODCAppEngine* ODCAppEngine::instance()
{
    return s_appEngineInstance;
}

ODCAppEngine::ODCAppEngine(QObject* parent)
    : QObject(parent)
{
    s_appEngineInstance = this;
    m_settingsManager = std::make_unique<ODCSettingsManager>(this);
    m_theme = std::make_unique<ODCTheme>(this);
}

void ODCAppEngine::initialize()
{
    if (m_initialized) return;

    registerBuiltinCommands();

    qInfo() << "OpenDroneCore Application Engine initialized";
    m_initialized = true;
    emit initializedChanged(true);
}

void ODCAppEngine::shutdown()
{
    if (!m_initialized) return;

    emit shutdownRequested();

    for (auto& plugin : m_plugins) {
        if (plugin.instance) {
            plugin.instance->shutdown();
            delete plugin.instance;
        }
    }
    m_plugins.clear();

    qInfo() << "OpenDroneCore Application Engine shutting down";
    m_initialized = false;
    emit initializedChanged(false);
}

void ODCAppEngine::showMessageDialog(const QString& title, const QString& message)
{
    emit messageDialogRequested(title, message);
}

QStringList ODCAppEngine::pluginList() const
{
    QStringList names;
    for (const auto& p : m_plugins) {
        if (p.instance) {
            names.append(p.instance->name());
        }
    }
    return names;
}

void ODCAppEngine::loadPlugin(const QString& pluginPath)
{
    QPluginLoader loader(pluginPath);
    QObject* instance = loader.instance();
    if (!instance) {
        qWarning() << "Failed to load plugin:" << pluginPath
                    << loader.errorString();
        return;
    }

    auto* plugin = qobject_cast<ODCPluginInterface*>(instance);
    if (!plugin) {
        qWarning() << "Plugin does not implement ODCPluginInterface:" << pluginPath;
        delete instance;
        return;
    }

    if (plugin->initialize(m_qmlEngine, this)) {
        LoadedPlugin lp;
        lp.instance = plugin;
        lp.path = pluginPath;
        m_plugins.append(lp);
        emit pluginListChanged();
        qInfo() << "Loaded plugin:" << plugin->name() << plugin->version();
    } else {
        qWarning() << "Plugin initialization failed:" << plugin->name();
        delete instance;
    }
}

QStringList ODCAppEngine::availablePlugins() const
{
    QStringList plugins;
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/plugins",
#ifdef Q_OS_LINUX
        "/usr/lib/opendronecore/plugins",
        "/usr/local/lib/opendronecore/plugins",
#endif
    };

    for (const auto& searchPath : searchPaths) {
        QDir dir(searchPath);
        if (!dir.exists()) continue;
        for (const auto& entry : dir.entryList(QDir::Files)) {
            if (QLibrary::isLibrary(entry)) {
                plugins.append(dir.absoluteFilePath(entry));
            }
        }
    }
    return plugins;
}

void ODCAppEngine::registerBuiltinCommands()
{
    auto* registry = ODCCommandRegistry::instance();
    auto* theme = m_theme.get();
    auto* settings = m_settingsManager.get();
    auto* notifHub = ODCNotificationHub::instance();
    auto* eventBus = ODCEventBus::instance();

    registry->registerCommand("toggleTheme", [theme](const QVariantMap&) {
        theme->toggleTheme();
        return QVariantMap{{"theme", theme->themeName()},
                           {"isDark", theme->isDark()}};
    });

    registry->registerCommand("setTheme", [theme](const QVariantMap& params) {
        int t = params.value("theme", 0).toInt();
        theme->setGlobalTheme(static_cast<ODCTheme::Theme>(t));
        return QVariantMap{{"theme", theme->themeName()},
                           {"isDark", theme->isDark()}};
    });

    registry->registerCommand("getTheme", [theme](const QVariantMap&) {
        return QVariantMap{{"theme", theme->themeName()},
                           {"isDark", theme->isDark()}};
    });

    registry->registerCommand("getSetting", [settings](const QVariantMap& params) {
        QVariant v = settings->value(params.value("group").toString(),
                                      params.value("key").toString());
        return QVariantMap{{"value", v}};
    });

    registry->registerCommand("setSetting", [settings, eventBus](const QVariantMap& params) {
        QString group = params.value("group").toString();
        QString key = params.value("key").toString();
        QVariant value = params.value("value");
        settings->setValue(group, key, value);
        eventBus->publishEvent("settingsChanged",
                               {{"group", group}, {"key", key}, {"value", value}});
        return QVariantMap{{"success", true}};
    });

    registry->registerCommand("getSettingKeys", [settings](const QVariantMap& params) {
        QStringList keys = settings->keys(params.value("group").toString());
        return QVariantMap{{"keys", QVariant::fromValue(keys)}};
    });

    registry->registerCommand("notify", [notifHub](const QVariantMap& params) {
        notifHub->notify(params.value("eventId").toString(),
                          params.value("data").toMap());
        return QVariantMap{{"success", true}};
    });

    registry->registerCommand("showMessage", [notifHub](const QVariantMap& params) {
        notifHub->notifyAlert(params.value("title").toString(),
                               params.value("message").toString(),
                               params.value("level", 0).toInt());
        return QVariantMap{{"success", true}};
    });

    registry->registerCommand("listCommands", [registry](const QVariantMap&) {
        return QVariantMap{{"commands", QVariant::fromValue(registry->registeredCommands())}};
    });

    qInfo() << "Registered" << registry->registeredCommands().size() << "built-in commands";
}
