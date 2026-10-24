#pragma once

#include <QString>
#include <QVariantMap>
#include <QObject>

class QQmlApplicationEngine;

class ODCPluginInterface
{
public:
    virtual ~ODCPluginInterface() = default;

    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;

    virtual bool initialize(QQmlApplicationEngine* engine, QObject* appEngine) = 0;
    virtual void shutdown() = 0;

    virtual QVariantMap metadata() const { return {}; }
};

#define ODCPluginInterface_iid "com.opendronecore.ODCPluginInterface"
Q_DECLARE_INTERFACE(ODCPluginInterface, ODCPluginInterface_iid)
