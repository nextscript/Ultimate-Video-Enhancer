#include "GlslPlugin.h"

namespace glslmgr {

bool GlslPlugin::initialize(uve::IPlayerHost *host, QString *error)
{
    Q_UNUSED(error);
    m_ctrl = new GlslController(host, QStringLiteral("org.uveplayer.glsl-manager"), this);
    return true;
}

} // namespace glslmgr
