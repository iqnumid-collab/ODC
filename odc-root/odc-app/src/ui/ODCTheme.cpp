#include "ODCTheme.h"
#include "ODCPalette.h"

ODCTheme* ODCTheme::s_instance = nullptr;

ODCTheme::ODCTheme(QObject* parent)
    : QObject(parent)
    , m_theme(Light)
{
    s_instance = this;
}

ODCTheme::~ODCTheme()
{
    s_instance = nullptr;
}

void ODCTheme::setGlobalTheme(Theme theme)
{
    if (m_theme != theme) {
        m_theme = theme;

        for (auto* palette : m_palettes) {
            palette->onThemeChanged(theme);
        }

        emit globalThemeChanged(theme);
    }
}

void ODCTheme::toggleTheme()
{
    setGlobalTheme(m_theme == Light ? Dark : Light);
}

QString ODCTheme::themeName() const
{
    return m_theme == Light ? QStringLiteral("Light") : QStringLiteral("Dark");
}

void ODCTheme::registerPalette(ODCPalette* palette)
{
    if (palette && !m_palettes.contains(palette)) {
        m_palettes.append(palette);
    }
}

void ODCTheme::unregisterPalette(ODCPalette* palette)
{
    m_palettes.removeAll(palette);
}
