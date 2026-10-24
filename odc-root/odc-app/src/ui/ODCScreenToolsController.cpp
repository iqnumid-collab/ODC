#include "ODCScreenToolsController.h"

#include <QSysInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QScreen>

ODCScreenToolsController::ODCScreenToolsController(QObject* parent)
    : QObject(parent)
{
    detectPlatform();

    QFont defaultFont;
    m_normalFontFamily = defaultFont.defaultFamily();

    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_fixedFontFamily = fixedFont.family();

    if (auto* screen = QGuiApplication::primaryScreen()) {
        m_devicePixelDensity = screen->physicalDotsPerInch();
    }

    connect(qGuiApp, &QGuiApplication::screenAdded,
            this, [this](QScreen* screen) {
        Q_UNUSED(screen);
        emit pixelSizeChanged();
    });
}

void ODCScreenToolsController::detectPlatform()
{
    m_serialAvailable = true;

#ifdef Q_OS_ANDROID
    m_normalFontFamily = QStringLiteral("OpenSans");
    m_fixedFontFamily = QStringLiteral("Courier New");
    m_serialAvailable = false;
#elif defined(Q_OS_IOS)
    m_normalFontFamily = QStringLiteral("OpenSans");
    m_fixedFontFamily = QStringLiteral("Courier New");
    m_serialAvailable = false;
#elif defined(Q_OS_WIN)
    m_normalFontFamily = QStringLiteral("OpenSans");
    m_fixedFontFamily = QStringLiteral("Consolas");
#elif defined(Q_OS_MACOS)
    m_normalFontFamily = QStringLiteral("OpenSans");
    m_fixedFontFamily = QStringLiteral("Monaco");
#else
    m_normalFontFamily = QStringLiteral("OpenSans");
    m_fixedFontFamily = QStringLiteral("Monospace");
#endif
}

bool ODCScreenToolsController::isAndroid() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

bool ODCScreenToolsController::isIOS() const
{
#ifdef Q_OS_IOS
    return true;
#else
    return false;
#endif
}

bool ODCScreenToolsController::isWindows() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool ODCScreenToolsController::isMacOS() const
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

bool ODCScreenToolsController::isLinux() const
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

bool ODCScreenToolsController::isDebug() const
{
#ifdef QT_DEBUG
    return true;
#else
    return false;
#endif
}

void ODCScreenToolsController::setFakeMobile(bool fake)
{
    if (m_fakeMobile != fake) {
        m_fakeMobile = fake;
        emit fakeMobileChanged();
    }
}

qreal ODCScreenToolsController::defaultFontDescent(int pointSize) const
{
    QFont font(m_normalFontFamily, pointSize);
    QFontMetrics fm(font);
    return fm.descent();
}

qreal ODCScreenToolsController::mouseX() const
{
    return m_mouseGlobalPos.x();
}

qreal ODCScreenToolsController::mouseY() const
{
    return m_mouseGlobalPos.y();
}

void ODCScreenToolsController::onMousePositionChanged(const QPoint& pos)
{
    m_mouseGlobalPos = pos;
    emit mousePositionChanged();
}

void ODCScreenToolsController::setDevicePixelDensity(qreal dpi)
{
    if (!qFuzzyCompare(m_devicePixelDensity, dpi)) {
        m_devicePixelDensity = dpi;
        emit devicePixelDensityChanged();
        emit pixelSizeChanged();
    }
}
