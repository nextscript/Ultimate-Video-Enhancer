#include "HotkeyModel.h"
#include "../core/AppSettings.h"

#include <QJsonObject>
#include <QKeySequence>

namespace uve {

QList<HotkeyModel::Action> HotkeyModel::defaultActions()
{
    // Belegung an VLC angelehnt; Strg+Alt+… bleibt für Effekte reserviert
    return {
        {"play.toggle", QT_TR_NOOP("Wiedergabe / Pause"), QT_TR_NOOP("Wiedergabe"), "Space"},
        {"play.stop", QT_TR_NOOP("Stopp"), QT_TR_NOOP("Wiedergabe"), "S"},
        {"play.next", QT_TR_NOOP("Nächster Titel"), QT_TR_NOOP("Wiedergabe"), "N"},
        {"play.prev", QT_TR_NOOP("Vorheriger Titel"), QT_TR_NOOP("Wiedergabe"), "P"},
        {"seek.back", QT_TR_NOOP("Zurückspulen (Sprungweite)"), QT_TR_NOOP("Wiedergabe"), "Left"},
        {"seek.fwd", QT_TR_NOOP("Vorspulen (Sprungweite)"), QT_TR_NOOP("Wiedergabe"), "Right"},
        {"seek.backShort", "3 s zurück", "Wiedergabe", "Shift+Left"},
        {"seek.fwdShort", "3 s vor", "Wiedergabe", "Shift+Right"},
        {"seek.backLong", "1 min zurück", "Wiedergabe", "Ctrl+Left"},
        {"seek.fwdLong", "1 min vor", "Wiedergabe", "Ctrl+Right"},
        {"frame.step", QT_TR_NOOP("Einzelbild vor"), QT_TR_NOOP("Wiedergabe"), "E"},
        {"frame.back", QT_TR_NOOP("Einzelbild zurück"), QT_TR_NOOP("Wiedergabe"), "Shift+E"},
        {"speed.up", QT_TR_NOOP("Schneller"), QT_TR_NOOP("Wiedergabe"), "]"},
        {"speed.down", QT_TR_NOOP("Langsamer"), QT_TR_NOOP("Wiedergabe"), "["},
        {"speed.reset", QT_TR_NOOP("Normale Geschwindigkeit"), QT_TR_NOOP("Wiedergabe"), "="},
        {"abloop", QT_TR_NOOP("A-B-Wiederholung"), QT_TR_NOOP("Wiedergabe"), "L"},
        {"repeat.cycle", QT_TR_NOOP("Wiederholen umschalten"), QT_TR_NOOP("Wiedergabe"), "R"},
        {"shuffle.toggle", QT_TR_NOOP("Zufallswiedergabe"), QT_TR_NOOP("Wiedergabe"), "Z"},
        {"goto", QT_TR_NOOP("Zu Zeit springen…"), QT_TR_NOOP("Wiedergabe"), "Ctrl+T"},
        {"volume.up", QT_TR_NOOP("Lauter"), QT_TR_NOOP("Audio"), "Up"},
        {"volume.down", QT_TR_NOOP("Leiser"), QT_TR_NOOP("Audio"), "Down"},
        {"mute", QT_TR_NOOP("Stumm"), QT_TR_NOOP("Audio"), "M"},
        {"audio.cycle", QT_TR_NOOP("Audiospur wechseln"), QT_TR_NOOP("Audio"), "B"},
        {"audio.delayMinus", "Audioverzögerung −50 ms", "Audio", "Ctrl+J"},
        {"audio.delayPlus", "Audioverzögerung +50 ms", "Audio", "Ctrl+K"},
        {"sub.cycle", QT_TR_NOOP("Untertitelspur wechseln"), QT_TR_NOOP("Untertitel"), "V"},
        {"sub.delayMinus", "Untertitelverzögerung −50 ms", "Untertitel", "G"},
        {"sub.delayPlus", "Untertitelverzögerung +50 ms", "Untertitel", "H"},
        {"fullscreen", QT_TR_NOOP("Vollbild"), QT_TR_NOOP("Video"), "F"},
        {"aspect.cycle", QT_TR_NOOP("Seitenverhältnis wechseln"), QT_TR_NOOP("Video"), "A"},
        {"snapshot", QT_TR_NOOP("Bildschirmfoto"), QT_TR_NOOP("Video"), "Shift+S"},
        {"diagnostics", QT_TR_NOOP("Diagnose-Overlay"), QT_TR_NOOP("Video"), "Ctrl+D"},
        {"effects.toggle", QT_TR_NOOP("Effekte an/aus"), QT_TR_NOOP("Effekte"), "Ctrl+Alt+E"},
        {"effects.compare", QT_TR_NOOP("Vorher/Nachher-Vergleich"), QT_TR_NOOP("Effekte"), "Ctrl+Alt+U"},
        {"open.file", QT_TR_NOOP("Datei öffnen…"), QT_TR_NOOP("Datei"), "Ctrl+O"},
        {"open.folder", QT_TR_NOOP("Ordner öffnen…"), QT_TR_NOOP("Datei"), "Ctrl+F"},
        {"open.url", QT_TR_NOOP("Netzwerkstream öffnen…"), QT_TR_NOOP("Datei"), "Ctrl+N"},
        {"playlist.save", QT_TR_NOOP("Wiedergabeliste speichern…"), QT_TR_NOOP("Datei"), "Ctrl+Y"},
        {"sidebar.toggle", QT_TR_NOOP("Seitenleiste ein/aus"), QT_TR_NOOP("Ansicht"), "Ctrl+L"},
        {"mediainfo", QT_TR_NOOP("Medieninformationen"), QT_TR_NOOP("Ansicht"), "Ctrl+I"},
        {"settings", QT_TR_NOOP("Einstellungen…"), QT_TR_NOOP("Ansicht"), "Ctrl+,"},
        {"quit", QT_TR_NOOP("Beenden"), QT_TR_NOOP("Datei"), "Ctrl+Q"},
    };
}

HotkeyModel::HotkeyModel(AppSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_actions(defaultActions())
{
    const auto o = m_settings->object(QStringLiteral("hotkeys"));
    for (auto it = o.begin(); it != o.end(); ++it) m_overrides.insert(it.key(), it.value().toString());
}

QString HotkeyModel::normalize(const QString &seq)
{
    return QKeySequence(seq, QKeySequence::PortableText).toString(QKeySequence::PortableText);
}

QString HotkeyModel::shortcut(const QString &id) const
{
    if (m_overrides.contains(id)) return m_overrides.value(id);
    for (const auto &a : m_actions) if (a.id == id) return a.defaultSeq;
    return {};
}

QString HotkeyModel::labelFor(const QString &id) const
{
    for (const auto &a : m_actions) if (a.id == id) return tr(qPrintable(a.label));
    return id;
}

QString HotkeyModel::sequenceFromKey(int key, int modifiers) const
{
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta || key == 0 || key == Qt::Key_unknown)
        return {};
    const int mods = modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    return QKeySequence(QKeyCombination(Qt::KeyboardModifiers(mods), Qt::Key(key))).toString(QKeySequence::PortableText);
}

QVariantList HotkeyModel::items() const
{
    QVariantList out;
    for (const auto &a : m_actions) {
        const QString cur = shortcut(a.id);
        out << QVariantMap{{"id", a.id}, {"label", tr(qPrintable(a.label))}, {"category", tr(qPrintable(a.category))},
                           {"shortcut", cur}, {"defaultShortcut", a.defaultSeq},
                           {"nativeText", QKeySequence(cur, QKeySequence::PortableText).toString(QKeySequence::NativeText)},
                           {"modified", m_overrides.contains(a.id)}};
    }
    return out;
}

QString HotkeyModel::conflictFor(const QString &id, const QString &seq) const
{
    const QString n = normalize(seq);
    if (n.isEmpty()) return {};
    for (const auto &a : m_actions) {
        if (a.id != id && normalize(shortcut(a.id)) == n) return a.id;
    }
    return {};
}

bool HotkeyModel::setShortcut(const QString &id, const QString &seq, bool force)
{
    const QString n = normalize(seq);
    const QString other = conflictFor(id, n);
    if (!other.isEmpty()) {
        if (!force) return false;
        m_overrides.insert(other, QString()); // andere Aktion verliert die Belegung
    }
    m_overrides.insert(id, n);
    save();
    return true;
}

void HotkeyModel::reset(const QString &id)
{
    m_overrides.remove(id);
    save();
}

void HotkeyModel::resetAll()
{
    m_overrides.clear();
    save();
}

void HotkeyModel::save()
{
    QJsonObject o;
    for (auto it = m_overrides.begin(); it != m_overrides.end(); ++it) {
        // Überschreibungen, die dem Standard entsprechen, nicht speichern
        bool same = false;
        for (const auto &a : m_actions) if (a.id == it.key() && normalize(a.defaultSeq) == it.value()) same = true;
        if (!same) o.insert(it.key(), it.value());
    }
    m_settings->setObject(QStringLiteral("hotkeys"), o);
    emit changed();
}

} // namespace uve
