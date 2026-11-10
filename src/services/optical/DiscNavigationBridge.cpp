#include "DiscNavigationBridge.h"

namespace uve::optical::nav {

QStringList actions(DiscKind kind)
{
    static const QStringList common{"up", "down", "left", "right", "select", "menu", "prev"};
    if (kind == DiscKind::Dvd) return common + QStringList{"title-menu", "popup"};
    if (kind == DiscKind::Bluray) return common + QStringList{"popup"};
    return {};
}

QStringList command(const QString &action, DiscKind kind, bool discActive, const DiscCapabilities &caps)
{
    if (!discActive || !caps.discnav || !caps.supports(kind)) return {};
    if (!actions(kind).contains(action)) return {};
    return {QStringLiteral("discnav"), action};
}

QStringList menuKeys() { return {"Up", "Down", "Left", "Right", "Return", "Enter", "Backspace"}; }

QString keyAction(const QString &key, bool menuActive)
{
    if (!menuActive) return {};
    if (key == QLatin1String("Up")) return QStringLiteral("up");
    if (key == QLatin1String("Down")) return QStringLiteral("down");
    if (key == QLatin1String("Left")) return QStringLiteral("left");
    if (key == QLatin1String("Right")) return QStringLiteral("right");
    if (key == QLatin1String("Return") || key == QLatin1String("Enter")) return QStringLiteral("select");
    if (key == QLatin1String("Backspace")) return QStringLiteral("prev");
    return {};
}

} // namespace uve::optical::nav
