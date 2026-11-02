#include "MpvVideoItem.h"
#include "MpvController.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>

namespace uve {

namespace {

void *getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void *>(ctx->getProcAddress(QByteArray(name))) : nullptr;
}

void onMpvRenderUpdate(void *ctx)
{
    // Aufruf aus einem mpv-Thread → nur eine Neuzeichnung im GUI-Thread anfordern
    QMetaObject::invokeMethod(static_cast<MpvVideoItem *>(ctx), &MpvVideoItem::scheduleUpdate, Qt::QueuedConnection);
}

class MpvRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit MpvRenderer(MpvVideoItem *item) : m_item(item) {}

    ~MpvRenderer() override
    {
        if (m_ctx) mpv_render_context_free(m_ctx);
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override
    {
        // Render-Kontext erst hier anlegen: der OpenGL-Kontext des Render-Threads ist aktuell
        if (!m_ctx && m_item->controller() && m_item->controller()->handle()) {
            mpv_opengl_init_params glInit{&getProcAddress, nullptr};
            // Standardmodus (ohne MPV_RENDER_PARAM_ADVANCED_CONTROL): mpv übernimmt das Frame-Timing
            mpv_render_param params[] = {
                {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
                {MPV_RENDER_PARAM_INVALID, nullptr},
            };
            if (mpv_render_context_create(&m_ctx, m_item->controller()->handle(), params) < 0) {
                qWarning("mpv_render_context_create fehlgeschlagen – keine Videoausgabe möglich");
                m_ctx = nullptr;
            } else {
                mpv_render_context_set_update_callback(m_ctx, &onMpvRenderUpdate, m_item);
                // Videoausgabe steht – zurückgestellte Dateien jetzt laden (GUI-Thread)
                QMetaObject::invokeMethod(m_item->controller(), &MpvController::markRenderReady, Qt::QueuedConnection);
            }
        }
        return QQuickFramebufferObject::Renderer::createFramebufferObject(size);
    }

    void render() override
    {
        if (!m_ctx) return;
        QOpenGLFramebufferObject *fbo = framebufferObject();
        mpv_opengl_fbo mfbo{static_cast<int>(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flipY = 0; // QQuickFramebufferObject spiegelt über mirrorVertically
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mfbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_ctx, params);
        QQuickOpenGLUtils::resetOpenGLState();
        QMetaObject::invokeMethod(m_item, &MpvVideoItem::noteFrame, Qt::QueuedConnection);
    }

private:
    MpvVideoItem *m_item;
    mpv_render_context *m_ctx = nullptr;
};

} // namespace

MpvVideoItem::MpvVideoItem(QQuickItem *parent) : QQuickFramebufferObject(parent)
{
    // Keine zusätzliche Spiegelung: mpv rendert mit flip_y=0 bereits passend für das Qt-Quick-FBO
    // (mit mirrorVertically stand das Bild im Selbsttest auf dem Kopf).
    setMirrorVertically(false);
}

QQuickFramebufferObject::Renderer *MpvVideoItem::createRenderer() const
{
    return new MpvRenderer(const_cast<MpvVideoItem *>(this));
}

QObject *MpvVideoItem::controllerObject() const { return m_controller; }

void MpvVideoItem::setControllerObject(QObject *c)
{
    auto *mc = qobject_cast<MpvController *>(c);
    if (mc == m_controller) return;
    m_controller = mc;
    emit controllerChanged();
    update();
}

void MpvVideoItem::scheduleUpdate() { update(); }

void MpvVideoItem::noteFrame()
{
    ++m_frames;
    if (m_frames < 3 || m_frames % 30 == 0) emit framesRenderedChanged();
}

} // namespace uve
