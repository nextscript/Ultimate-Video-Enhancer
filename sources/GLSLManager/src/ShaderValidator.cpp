#include "ShaderValidator.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRegularExpression>
#include <QStringList>
#include <QSurfaceFormat>

namespace glslmgr {

namespace {

constexpr int kMaxMessages = 4;

QString bindDeclarations(const QString &name)
{
    return QStringLiteral(
               "uniform sampler2D %1_raw;\n"
               "in vec2 %1_pos;\n"
               "uniform vec2 %1_size;\n"
               "uniform mat2 %1_rot;\n"
               "uniform vec2 %1_off;\n"
               "uniform vec2 %1_pt;\n"
               "uniform float %1_map;\n"
               "#define %1_mul 1.000000\n"
               "#define %1_tex(pos) (%1_mul * vec4(texture(%1_raw, pos)).rgba)\n"
               "#define %1_gather(pos, c) (%1_mul * vec4(textureGather(%1_raw, pos, c)))\n"
               "#define %1_texOff(off) %1_tex(%1_pos + %1_pt * vec2(off))\n")
        .arg(name);
}

// Zeilennummer aus dem Treiberlog ("0(67) :", "ERROR: 0:67:") auf die Quellzeile abbilden
QString describeLog(const QString &log, const QString &source)
{
    static const QRegularExpression lineRe(QStringLiteral(R"(\b0[:(](\d+)\)?)"));
    const QStringList src = source.split(QLatin1Char('\n'));
    QStringList out;
    for (const auto &raw : log.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || !line.contains(QLatin1String("error"), Qt::CaseInsensitive)) continue;
        QString text = line;
        const auto m = lineRe.match(line);
        if (m.hasMatch()) {
            const QString code = src.value(m.captured(1).toInt() - 1).trimmed();
            if (!code.isEmpty()) text += QStringLiteral("\n  > ") + code.left(120);
        }
        out << text;
        if (out.size() >= kMaxMessages) break;
    }
    return out.isEmpty() ? log.trimmed().left(600) : out.join(QLatin1Char('\n'));
}

} // namespace

QStringList fragmentSources(const QString &hookText, int glslVersion)
{
    QStringList out;
    QStringList binds, body;
    bool inBody = false, any = false;

    const auto finish = [&] {
        if (!any) return;
        QString s = QStringLiteral("#version %1\n").arg(glslVersion);
        s += QStringLiteral("uniform int frame;\nuniform float random;\nuniform vec2 input_size;\nuniform vec2 target_size;\nuniform vec2 tex_offset;\nout vec4 gvf_out_color;\n");
        for (const auto &b : std::as_const(binds)) s += bindDeclarations(b);
        s += body.join(QLatin1Char('\n'));
        s += QStringLiteral("\nvoid main() { gvf_out_color = hook(); }\n");
        out << s;
        binds.clear();
        body.clear();
        inBody = false;
        any = false;
    };

    for (const auto &line : hookText.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.startsWith(QLatin1String("//!"))) {
            if (inBody) finish();
            any = true;
            const QStringList parts = t.mid(3).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            const QString cmd = parts.value(0);
            if (cmd == QLatin1String("TEXTURE") || cmd == QLatin1String("BUFFER") || cmd == QLatin1String("COMPUTE")) return {};
            if (cmd == QLatin1String("BIND") && parts.size() > 1 && !binds.contains(parts[1])) binds << parts[1];
            continue;
        }
        if (any) {
            if (!t.isEmpty()) inBody = true;
            body << line;
        }
    }
    finish();
    return out;
}

ValidationResult validateHookShader(const QString &hookText)
{
    QOpenGLContext *previous = QOpenGLContext::currentContext();
    QSurface *previousSurface = previous ? previous->surface() : nullptr;

    ValidationResult result;
    {
        QOpenGLContext ctx;
        ctx.setFormat(QSurfaceFormat::defaultFormat());
        if (!ctx.create()) return {};
        QOffscreenSurface surface;
        surface.setFormat(ctx.format());
        surface.create();
        if (!surface.isValid() || !ctx.makeCurrent(&surface)) {
            if (previous) previous->makeCurrent(previousSurface);
            return {};
        }
        result.checked = true;
        const auto fmt = ctx.format();
        const int version = (fmt.majorVersion() > 4 || (fmt.majorVersion() == 4 && fmt.minorVersion() >= 4)) ? 440 : 330;
        QOpenGLFunctions *gl = ctx.functions();
        const QStringList sources = (fmt.majorVersion() >= 3 && !ctx.isOpenGLES()) ? fragmentSources(hookText, version) : QStringList();
        if (sources.isEmpty()) result.checked = false;
        for (const auto &source : sources) {
            const QByteArray bytes = source.toUtf8();
            const char *ptr = bytes.constData();
            const GLuint shader = gl->glCreateShader(GL_FRAGMENT_SHADER);
            gl->glShaderSource(shader, 1, &ptr, nullptr);
            gl->glCompileShader(shader);
            GLint ok = GL_FALSE, length = 0;
            gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (ok != GL_TRUE) {
                gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
                QByteArray log(qMax(length, 1), '\0');
                gl->glGetShaderInfoLog(shader, log.size(), nullptr, log.data());
                result.error = describeLog(QString::fromUtf8(log.constData()), source);
            }
            gl->glDeleteShader(shader);
            if (!result.error.isEmpty()) break;
        }
        ctx.doneCurrent();
    }
    if (previous) previous->makeCurrent(previousSurface);
    return result;
}

} // namespace glslmgr
