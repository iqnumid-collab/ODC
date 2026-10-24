#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QFont>
#include <QtQml/qqmlregistration.h>

class ODCScreenToolsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString normalFontFamily READ normalFontFamily CONSTANT)
    Q_PROPERTY(QString fixedFontFamily READ fixedFontFamily CONSTANT)
    Q_PROPERTY(bool isAndroid READ isAndroid CONSTANT)
    Q_PROPERTY(bool isIOS READ isIOS CONSTANT)
    Q_PROPERTY(bool isMobile READ isMobile CONSTANT)
    Q_PROPERTY(bool isWindows READ isWindows CONSTANT)
    Q_PROPERTY(bool isMacOS READ isMacOS CONSTANT)
    Q_PROPERTY(bool isLinux READ isLinux CONSTANT)
    Q_PROPERTY(bool isDebug READ isDebug CONSTANT)
    Q_PROPERTY(bool isFakeMobile READ isFakeMobile WRITE setFakeMobile NOTIFY fakeMobileChanged)
    Q_PROPERTY(bool isSerialAvailable READ isSerialAvailable CONSTANT)
    Q_PROPERTY(QPoint mouseGlobalPosition READ mouseGlobalPosition NOTIFY mousePositionChanged)
    Q_PROPERTY(qreal devicePixelDensity READ devicePixelDensity NOTIFY devicePixelDensityChanged)

public:
    explicit ODCScreenToolsController(QObject* parent = nullptr);
    ~ODCScreenToolsController() override = default;

    QString normalFontFamily() const { return m_normalFontFamily; }
    QString fixedFontFamily() const { return m_fixedFontFamily; }

    bool isAndroid() const;
    bool isIOS() const;
    bool isMobile() const { return isAndroid() || isIOS(); }
    bool isWindows() const;
    bool isMacOS() const;
    bool isLinux() const;
    bool isDebug() const;
    bool isFakeMobile() const { return m_fakeMobile; }
    void setFakeMobile(bool fake);
    bool isSerialAvailable() const { return m_serialAvailable; }

    QPoint mouseGlobalPosition() const { return m_mouseGlobalPos; }
    qreal devicePixelDensity() const { return m_devicePixelDensity; }

    Q_INVOKABLE void setDevicePixelDensity(qreal dpi);

    Q_INVOKABLE qreal defaultFontDescent(int pointSize) const;
    Q_INVOKABLE qreal mouseX() const;
    Q_INVOKABLE qreal mouseY() const;

public slots:
    void onMousePositionChanged(const QPoint& pos);

signals:
    void fakeMobileChanged();
    void mousePositionChanged();
    void devicePixelDensityChanged();
    void pixelSizeChanged();

private:
    void detectPlatform();

    QString m_normalFontFamily;
    QString m_fixedFontFamily;
    bool m_fakeMobile = false;
    bool m_serialAvailable = false;
    QPoint m_mouseGlobalPos;
    qreal m_devicePixelDensity = 96.0;
};
