#include "MpvVideoItem.h"
#include "MpvController.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
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
        delete m_mid;
        delete m_small;
        if (m_pbo)
            if (QOpenGLContext *ctx = QOpenGLContext::currentContext()) ctx->extraFunctions()->glDeleteBuffers(1, &m_pbo);
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
        sampleOutput(fbo);
        QQuickOpenGLUtils::resetOpenGLState();
        QMetaObject::invokeMethod(m_item, &MpvVideoItem::noteFrame, Qt::QueuedConnection);
    }

private:
    // Bildprobe für Plugins (Auto-Scene-Match): Videofläche per GPU auf 96×54 verkleinern (zweistufig, linear)
    // und asynchron über einen Pixel-Pack-Puffer zurücklesen – abgeholt beim nächsten Frame, kein Warten auf die GPU.
    void sampleOutput(QOpenGLFramebufferObject *fbo)
    {
        MpvController *mc = m_item->controller();
        if (!mc) return;
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (!ctx) return;
        QOpenGLExtraFunctions *gl = ctx->extraFunctions();
        constexpr int SW = 96, SH = 54, MW = 384, MH = 216;

        if (m_pboPending) {   // Probe aus dem vorigen Frame abholen
            m_pboPending = false;
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
            if (const void *p = gl->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, SW * SH * 4, GL_MAP_READ_BIT)) {
                QImage img(static_cast<const uchar *>(p), SW, SH, SW * 4, QImage::Format_RGBA8888);
                mc->storeDisplaySample(img.mirrored().convertToFormat(QImage::Format_RGB32));   // GL: Zeilen von unten
                gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
        if (!mc->takeDisplaySampleRequest()) return;

        if (!m_mid) m_mid = new QOpenGLFramebufferObject(MW, MH);
        if (!m_small) m_small = new QOpenGLFramebufferObject(SW, SH);
        if (!m_pbo) {
            gl->glGenBuffers(1, &m_pbo);
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
            gl->glBufferData(GL_PIXEL_PACK_BUFFER, SW * SH * 4, nullptr, GL_STREAM_READ);
            gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
        // Nur die sichtbare Videofläche (ohne schwarze Ränder); GL-Ursprung unten links
        const QRectF a = mc->videoAreaFraction();
        const int fw = fbo->width(), fh = fbo->height();
        QRect src(int(a.x() * fw), int((1.0 - a.y() - a.height()) * fh), int(a.width() * fw), int(a.height() * fh));
        src = src.intersected(QRect(0, 0, fw, fh));
        if (src.width() < 2 || src.height() < 2) return;
        QOpenGLFramebufferObject::blitFramebuffer(m_mid, QRect(0, 0, MW, MH), fbo, src, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        QOpenGLFramebufferObject::blitFramebuffer(m_small, QRect(0, 0, SW, SH), m_mid, QRect(0, 0, MW, MH), GL_COLOR_BUFFER_BIT, GL_LINEAR);
        m_small->bind();
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbo);
        gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
        gl->glReadPixels(0, 0, SW, SH, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);   // asynchron in den PBO
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        m_small->release();
        m_pboPending = true;
    }

    MpvVideoItem *m_item;
    mpv_render_context *m_ctx = nullptr;
    QOpenGLFramebufferObject *m_mid = nullptr, *m_small = nullptr;
    GLuint m_pbo = 0;
    bool m_pboPending = false;
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
