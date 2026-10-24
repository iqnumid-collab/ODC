#pragma once

#include <QObject>
#include <QColor>
#include <QtQml/qqmlregistration.h>

#include "ODCTheme.h"

class ODCPalette : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // Group enabled state
    Q_PROPERTY(bool colorGroupEnabled READ colorGroupEnabled WRITE setColorGroupEnabled NOTIFY paletteChanged)

    // Global theme (read-only, driven by ODCTheme singleton)
    Q_PROPERTY(ODCTheme::Theme globalTheme READ globalTheme NOTIFY paletteChanged)

    // Semantic color tokens
    Q_PROPERTY(QColor window READ window NOTIFY paletteChanged)
    Q_PROPERTY(QColor windowShade READ windowShade NOTIFY paletteChanged)
    Q_PROPERTY(QColor text READ text NOTIFY paletteChanged)
    Q_PROPERTY(QColor button READ button NOTIFY paletteChanged)
    Q_PROPERTY(QColor buttonText READ buttonText NOTIFY paletteChanged)
    Q_PROPERTY(QColor buttonHighlight READ buttonHighlight NOTIFY paletteChanged)
    Q_PROPERTY(QColor buttonHighlightText READ buttonHighlightText NOTIFY paletteChanged)
    Q_PROPERTY(QColor buttonBorder READ buttonBorder NOTIFY paletteChanged)
    Q_PROPERTY(QColor primaryButton READ primaryButton NOTIFY paletteChanged)
    Q_PROPERTY(QColor primaryButtonText READ primaryButtonText NOTIFY paletteChanged)
    Q_PROPERTY(QColor toolbarBackground READ toolbarBackground NOTIFY paletteChanged)
    Q_PROPERTY(QColor toolbarText READ toolbarText NOTIFY paletteChanged)
    Q_PROPERTY(QColor alertBackground READ alertBackground NOTIFY paletteChanged)
    Q_PROPERTY(QColor alertText READ alertText NOTIFY paletteChanged)
    Q_PROPERTY(QColor alertBorder READ alertBorder NOTIFY paletteChanged)
    Q_PROPERTY(QColor colorGreen READ colorGreen NOTIFY paletteChanged)
    Q_PROPERTY(QColor colorOrange READ colorOrange NOTIFY paletteChanged)
    Q_PROPERTY(QColor colorRed READ colorRed NOTIFY paletteChanged)
    Q_PROPERTY(QColor colorBlue READ colorBlue NOTIFY paletteChanged)
    Q_PROPERTY(QColor colorGrey READ colorGrey NOTIFY paletteChanged)
    Q_PROPERTY(QColor mapWidget READ mapWidget NOTIFY paletteChanged)
    Q_PROPERTY(QColor mapWidgetBorder READ mapWidgetBorder NOTIFY paletteChanged)
    Q_PROPERTY(QColor brandLogoBlue READ brandLogoBlue NOTIFY paletteChanged)
    Q_PROPERTY(QColor warningText READ warningText NOTIFY paletteChanged)
    Q_PROPERTY(QColor statusFailed READ statusFailed NOTIFY paletteChanged)
    Q_PROPERTY(QColor statusPassed READ statusPassed NOTIFY paletteChanged)
    Q_PROPERTY(QColor windowShadeLight READ windowShadeLight NOTIFY paletteChanged)
    Q_PROPERTY(QColor windowShadeDark READ windowShadeDark NOTIFY paletteChanged)
    Q_PROPERTY(QColor windowTransparent READ windowTransparent NOTIFY paletteChanged)
    Q_PROPERTY(QColor textField READ textField NOTIFY paletteChanged)
    Q_PROPERTY(QColor textFieldText READ textFieldText NOTIFY paletteChanged)
    Q_PROPERTY(QColor groupBorder READ groupBorder NOTIFY paletteChanged)
    Q_PROPERTY(QColor mapWidgetBorderLight READ mapWidgetBorderLight NOTIFY paletteChanged)
    Q_PROPERTY(QColor mapWidgetBorderDark READ mapWidgetBorderDark NOTIFY paletteChanged)
    Q_PROPERTY(QColor toolStripHoverColor READ toolStripHoverColor NOTIFY paletteChanged)
    Q_PROPERTY(QVariantList colors READ colors NOTIFY paletteChanged)

public:
    explicit ODCPalette(QObject* parent = nullptr);
    ~ODCPalette() override;

    bool colorGroupEnabled() const { return m_colorGroupEnabled; }
    void setColorGroupEnabled(bool enabled);

    ODCTheme::Theme globalTheme() const;
    void setThemeObject(ODCTheme* theme);

    // Called by ODCTheme when global theme changes
    void onThemeChanged(ODCTheme::Theme theme);

    QColor window() const;
    QColor windowShade() const;
    QColor text() const;
    QColor button() const;
    QColor buttonText() const;
    QColor buttonHighlight() const;
    QColor buttonHighlightText() const;
    QColor buttonBorder() const;
    QColor primaryButton() const;
    QColor primaryButtonText() const;
    QColor toolbarBackground() const;
    QColor toolbarText() const;
    QColor alertBackground() const;
    QColor alertText() const;
    QColor alertBorder() const;
    QColor colorGreen() const;
    QColor colorOrange() const;
    QColor colorRed() const;
    QColor colorBlue() const;
    QColor colorGrey() const;
    QColor mapWidget() const;
    QColor mapWidgetBorder() const;
    QColor brandLogoBlue() const;
    QColor warningText() const;
    QColor statusFailed() const;
    QColor statusPassed() const;
    QColor windowShadeLight() const;
    QColor windowShadeDark() const;
    QColor windowTransparent() const;
    QColor textField() const;
    QColor textFieldText() const;
    QColor groupBorder() const;
    QColor mapWidgetBorderLight() const;
    QColor mapWidgetBorderDark() const;
    QColor toolStripHoverColor() const;
    QVariantList colors() const;

signals:
    void paletteChanged();

private:
    QColor resolve(const QColor& lightEnabled, const QColor& lightDisabled,
                   const QColor& darkEnabled, const QColor& darkDisabled) const;

    bool m_colorGroupEnabled = true;
    ODCTheme* m_theme = nullptr;
    ODCTheme::Theme m_currentTheme = ODCTheme::Light;
};
