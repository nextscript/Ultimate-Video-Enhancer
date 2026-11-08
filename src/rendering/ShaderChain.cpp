#include "ShaderChain.h"
#include "../core/Logger.h"
#include "../player/MpvController.h"

#include <QFileInfo>

namespace uve {

ShaderChain::ShaderChain(MpvController *mpv, QObject *parent) : QObject(parent), m_mpv(mpv)
{
    connect(m_mpv, &MpvController::shaderError, this, &ShaderChain::onMpvError);
}

void ShaderChain::setStage(const QString &owner, const QString &file)
{
    if (!m_order.contains(owner)) m_order << owner;
    // Eigene Stufe eines Plugins ("<pluginId>#<name>"): eine neue Datei darf nach einem Fehler wieder laufen
    if (owner.contains(QLatin1Char('#'))) m_disabled.remove(owner);
    if (file.isEmpty()) m_files.remove(owner);
    else m_files.insert(owner, file);
    m_lastChanged = owner;
    apply();
}

QStringList ShaderChain::activeShaders() const
{
    QStringList out;
    for (const auto &o : m_order) {
        if (m_disabled.contains(o)) continue;
        // Eine Stufe kann aus mehreren Dateien bestehen (durch Zeilenumbruch getrennt, z. B. LUT-Textur + Pässe)
        for (const auto &f : m_files.value(o).split('\n', Qt::SkipEmptyParts))
            if (QFileInfo::exists(f)) out << f;
    }
    return out;
}

void ShaderChain::apply()
{
    const auto list = activeShaders();
    // Liste komplett setzen (mpv lädt geänderte Dateien neu)
    m_mpv->setProperty(QStringLiteral("glsl-shaders"), QVariant(list));
    emit changed();
}

void ShaderChain::removeSubStages(const QString &pluginId)
{
    const QString prefix = pluginId + QLatin1Char('#');
    bool any = false;
    for (const auto &o : m_order) {
        if (!o.startsWith(prefix)) continue;
        if (m_files.remove(o)) any = true;
        m_disabled.remove(o);
    }
    if (any) apply();
}

void ShaderChain::reenable(const QString &owner)
{
    m_disabled.remove(owner);
    apply();
}

void ShaderChain::onMpvError(const QString &text)
{
    // Nur echte Shader-Fehler (keine allgemeinen VO-Warnungen)
    const bool shaderRelated = text.contains(QLatin1String("shader"), Qt::CaseInsensitive) ||
                               text.contains(QLatin1String("GLSL"), Qt::CaseInsensitive) ||
                               text.contains(QLatin1String("compile"), Qt::CaseInsensitive) ||
                               text.contains(QLatin1String("hook"), Qt::CaseInsensitive);
    if (!shaderRelated) return;
    Logger::write("shader", text);
    QString owner;
    for (const auto &o : m_order) {
        bool match = text.contains(o + ':');
        for (const auto &f : m_files.value(o).split('\n', Qt::SkipEmptyParts)) match = match || text.contains(QFileInfo(f).fileName());
        if (match) { owner = o; break; }
    }
    if (owner.isEmpty()) owner = m_lastChanged;
    if (owner.isEmpty() || m_disabled.contains(owner)) return;
    m_lastError = text;
    m_disabled.insert(owner, text);
    apply();
    emit errorOccurred(owner, text);
}

} // namespace uve
