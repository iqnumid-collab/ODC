#include "ODCPalette.h"
#include "ODCTheme.h"

#include <QGuiApplication>

ODCPalette::ODCPalette(QObject* parent)
    : QObject(parent)
{
    if (ODCTheme::s_instance) {
        setThemeObject(ODCTheme::s_instance);
    }
}

ODCPalette::~ODCPalette()
{
    if (m_theme) {
        m_theme->unregisterPalette(this);
    }
}

void ODCPalette::setColorGroupEnabled(bool enabled)
{
    if (m_colorGroupEnabled != enabled) {
        m_colorGroupEnabled = enabled;
        emit paletteChanged();
    }
}

ODCTheme::Theme ODCPalette::globalTheme() const
{
    return m_currentTheme;
}

void ODCPalette::setThemeObject(ODCTheme* theme)
{
    if (m_theme != theme) {
        if (m_theme) {
            m_theme->unregisterPalette(this);
        }
        m_theme = theme;
        if (m_theme) {
            m_theme->registerPalette(this);
            m_currentTheme = m_theme->globalTheme();
        }
        emit paletteChanged();
    }
}

void ODCPalette::onThemeChanged(ODCTheme::Theme theme)
{
    if (m_currentTheme != theme) {
        m_currentTheme = theme;
        emit paletteChanged();
    }
}

QColor ODCPalette::resolve(const QColor& lightEnabled, const QColor& lightDisabled,
                            const QColor& darkEnabled, const QColor& darkDisabled) const
{
    if (m_currentTheme == ODCTheme::Dark) {
        return m_colorGroupEnabled ? darkEnabled : darkDisabled;
    }
    return m_colorGroupEnabled ? lightEnabled : lightDisabled;
}

QColor ODCPalette::window() const
{
    return resolve(QColor(QRgb(0xECECEC)), QColor(QRgb(0xECECEC)),
                   QColor(QRgb(0x2B2B2B)), QColor(QRgb(0x2B2B2B)));
}

QColor ODCPalette::windowShade() const
{
    return resolve(QColor(QRgb(0xD3D3D3)), QColor(QRgb(0xD3D3D3)),
                   QColor(QRgb(0x3C3C3C)), QColor(QRgb(0x3C3C3C)));
}

QColor ODCPalette::text() const
{
    return resolve(QColor(QRgb(0x000000)), QColor(QRgb(0x888888)),
                   QColor(QRgb(0xE0E0E0)), QColor(QRgb(0x707070)));
}

QColor ODCPalette::button() const
{
    return resolve(QColor(QRgb(0xFFFFFF)), QColor(QRgb(0xFFFFFF)),
                   QColor(QRgb(0x4A4A4A)), QColor(QRgb(0x4A4A4A)));
}

QColor ODCPalette::buttonText() const
{
    return resolve(QColor(QRgb(0x000000)), QColor(QRgb(0x999999)),
                   QColor(QRgb(0xE0E0E0)), QColor(QRgb(0x707070)));
}

QColor ODCPalette::buttonHighlight() const
{
    return resolve(QColor(QRgb(0x8CB4E3)), QColor(QRgb(0x8CB4E3)),
                   QColor(QRgb(0x5A8EC7)), QColor(QRgb(0x5A8EC7)));
}

QColor ODCPalette::buttonHighlightText() const
{
    return resolve(QColor(QRgb(0xFFFFFF)), QColor(QRgb(0xFFFFFF)),
                   QColor(QRgb(0xFFFFFF)), QColor(QRgb(0xFFFFFF)));
}

QColor ODCPalette::buttonBorder() const
{
    return resolve(QColor(QRgb(0xBDBDBD)), QColor(QRgb(0xBDBDBD)),
                   QColor(QRgb(0x555555)), QColor(QRgb(0x555555)));
}

QColor ODCPalette::primaryButton() const
{
    return resolve(QColor(QRgb(0x3B7DD4)), QColor(QRgb(0x3B7DD4)),
                   QColor(QRgb(0x3B7DD4)), QColor(QRgb(0x3B7DD4)));
}

QColor ODCPalette::primaryButtonText() const
{
    return resolve(QColor(QRgb(0xFFFFFF)), QColor(QRgb(0xFFFFFF)),
                   QColor(QRgb(0xFFFFFF)), QColor(QRgb(0xFFFFFF)));
}

QColor ODCPalette::toolbarBackground() const
{
    return resolve(QColor(QRgb(0x333333)), QColor(QRgb(0x333333)),
                   QColor(QRgb(0x1E1E1E)), QColor(QRgb(0x1E1E1E)));
}

QColor ODCPalette::toolbarText() const
{
    return resolve(QColor(QRgb(0xFFFFFF)), QColor(QRgb(0x888888)),
                   QColor(QRgb(0xE0E0E0)), QColor(QRgb(0x888888)));
}

QColor ODCPalette::alertBackground() const
{
    return resolve(QColor(QRgb(0xFAD7B6)), QColor(QRgb(0xFAD7B6)),
                   QColor(QRgb(0x804000)), QColor(QRgb(0x804000)));
}

QColor ODCPalette::alertText() const
{
    return resolve(QColor(QRgb(0x000000)), QColor(QRgb(0x000000)),
                   QColor(QRgb(0xE0E0E0)), QColor(QRgb(0xE0E0E0)));
}

QColor ODCPalette::alertBorder() const
{
    return resolve(QColor(QRgb(0xF07032)), QColor(QRgb(0xF07032)),
                   QColor(QRgb(0xF07032)), QColor(QRgb(0xF07032)));
}

QColor ODCPalette::colorGreen() const
{
    return resolve(QColor(QRgb(0x28A745)), QColor(QRgb(0x176427)),
                   QColor(QRgb(0x28A745)), QColor(QRgb(0x176427)));
}

QColor ODCPalette::colorOrange() const
{
    return resolve(QColor(QRgb(0xFD7E14)), QColor(QRgb(0x964B0B)),
                   QColor(QRgb(0xFD7E14)), QColor(QRgb(0x964B0B)));
}

QColor ODCPalette::colorRed() const
{
    return resolve(QColor(QRgb(0xDC3545)), QColor(QRgb(0x8B1F2B)),
                   QColor(QRgb(0xDC3545)), QColor(QRgb(0x8B1F2B)));
}

QColor ODCPalette::colorBlue() const
{
    return resolve(QColor(QRgb(0x3B7DD4)), QColor(QRgb(0x245189)),
                   QColor(QRgb(0x3B7DD4)), QColor(QRgb(0x245189)));
}

QColor ODCPalette::colorGrey() const
{
    return resolve(QColor(QRgb(0x808080)), QColor(QRgb(0x4D4D4D)),
                   QColor(QRgb(0x808080)), QColor(QRgb(0x4D4D4D)));
}

QColor ODCPalette::mapWidget() const
{
    return resolve(QColor(QRgb(0xFFFFFF)), QColor(QRgb(0xDDDDDD)),
                   QColor(QRgb(0x4A4A4A)), QColor(QRgb(0x3A3A3A)));
}

QColor ODCPalette::mapWidgetBorder() const
{
    return resolve(QColor(QRgb(0xBDBDBD)), QColor(QRgb(0xBDBDBD)),
                   QColor(QRgb(0x555555)), QColor(QRgb(0x555555)));
}

QColor ODCPalette::brandLogoBlue() const
{
    return QColor(QRgb(0x3B7DD4));
}

QColor ODCPalette::warningText() const
{
    return resolve(QColor(QRgb(0xCC6600)), QColor(QRgb(0xCC6600)),
                   QColor(QRgb(0xFF9900)), QColor(QRgb(0xFF9900)));
}

QColor ODCPalette::statusFailed() const
{
    return colorRed();
}

QColor ODCPalette::statusPassed() const
{
    return colorGreen();
}

QColor ODCPalette::windowShadeLight() const
{
    return resolve(QColor(QRgb(0xE0E0E0)), QColor(QRgb(0xE0E0E0)),
                   QColor(QRgb(0x484848)), QColor(QRgb(0x484848)));
}

QColor ODCPalette::windowShadeDark() const
{
    return resolve(QColor(QRgb(0xC0C0C0)), QColor(QRgb(0xC0C0C0)),
                   QColor(QRgb(0x222222)), QColor(QRgb(0x222222)));
}

QColor ODCPalette::windowTransparent() const
{
    return resolve(QColor(0xDD, 0xDD, 0xDD, 0x80), QColor(0xDD, 0xDD, 0xDD, 0x80),
                   QColor(0x33, 0x33, 0x33, 0x80), QColor(0x33, 0x33, 0x33, 0x80));
}

QColor ODCPalette::textField() const
{
    return resolve(QColor(QRgb(0xFFFFFF)), QColor(QRgb(0xF0F0F0)),
                   QColor(QRgb(0x3A3A3A)), QColor(QRgb(0x2A2A2A)));
}

QColor ODCPalette::textFieldText() const
{
    return resolve(QColor(QRgb(0x000000)), QColor(QRgb(0x888888)),
                   QColor(QRgb(0xE0E0E0)), QColor(QRgb(0x707070)));
}

QColor ODCPalette::groupBorder() const
{
    return resolve(QColor(QRgb(0x888888)), QColor(QRgb(0x888888)),
                   QColor(QRgb(0x666666)), QColor(QRgb(0x666666)));
}

QColor ODCPalette::mapWidgetBorderLight() const
{
    return resolve(QColor(QRgb(0xDDDDDD)), QColor(QRgb(0xDDDDDD)),
                   QColor(QRgb(0x666666)), QColor(QRgb(0x666666)));
}

QColor ODCPalette::mapWidgetBorderDark() const
{
    return resolve(QColor(QRgb(0x888888)), QColor(QRgb(0x888888)),
                   QColor(QRgb(0x444444)), QColor(QRgb(0x444444)));
}

QColor ODCPalette::toolStripHoverColor() const
{
    return resolve(QColor(QRgb(0x8CB4E3)), QColor(QRgb(0x8CB4E3)),
                   QColor(QRgb(0x5A8EC7)), QColor(QRgb(0x5A8EC7)));
}

QVariantList ODCPalette::colors() const
{
    return {
        window(), windowShade(), text(), button(), buttonText(),
        buttonHighlight(), buttonHighlightText(), colorGreen(),
        colorOrange(), colorRed(), colorBlue(), colorGrey()
    };
}
