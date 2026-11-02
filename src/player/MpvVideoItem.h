#pragma once
#include <QQuickFramebufferObject>

namespace uve {

class MpvController;

// QML-Element "MpvVideo" (registriert in main.cpp), in das libmpv über die Render-API (OpenGL) zeichnet.
// Voraussetzung: Qt Quick läuft mit dem OpenGL-RHI-Backend (siehe main.cpp).
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controllerObject WRITE setControllerObject NOTIFY controllerChanged)
    Q_PROPERTY(int framesRendered READ framesRendered NOTIFY framesRenderedChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);
    Renderer *createRenderer() const override;

    MpvController *controller() const { return m_controller; }
    QObject *controllerObject() const;
    void setControllerObject(QObject *c);
    int framesRendered() const { return m_frames; }
    void noteFrame();

signals:
    void controllerChanged();
    void framesRenderedChanged();

public slots:
    void scheduleUpdate();

private:
    MpvController *m_controller = nullptr;
    int m_frames = 0;
};

} // namespace uve
