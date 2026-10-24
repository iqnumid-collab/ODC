#pragma once

#include <QObject>
#include <QColor>
#include <QString>

class ODCPalette;

class ODCTheme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Theme globalTheme READ globalTheme WRITE setGlobalTheme NOTIFY globalThemeChanged)
    Q_PROPERTY(bool isDark READ isDark NOTIFY globalThemeChanged)

public:
    enum Theme {
        Light = 0,
        Dark = 1
    };
    Q_ENUM(Theme)

    explicit ODCTheme(QObject* parent = nullptr);
    ~ODCTheme() override;

    static ODCTheme* s_instance;

    Theme globalTheme() const { return m_theme; }
    void setGlobalTheme(Theme theme);
    bool isDark() const { return m_theme == Dark; }

    Q_INVOKABLE void toggleTheme();
    Q_INVOKABLE QString themeName() const;
    Q_INVOKABLE bool isLight() const { return m_theme == Light; }

    void registerPalette(ODCPalette* palette);
    void unregisterPalette(ODCPalette* palette);

signals:
    void globalThemeChanged(ODCTheme::Theme theme);

private:
    Theme m_theme = Light;
    QList<ODCPalette*> m_palettes;
};
