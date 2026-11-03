#pragma once
#include <QObject>
#include <QVariantList>

namespace uve {

class AppSettings;

// Editierbare Tastenkürzel mit Konfliktprüfung und Rücksetzen auf Standard.
// Gespeichert werden nur Abweichungen vom Standard (settings.json → "hotkeys").
class HotkeyModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
public:
    // external = von einem Plugin (Texte kommen schon übersetzt, nicht über tr())
    struct Action { QString id; QString label; QString category; QString defaultSeq; bool external = false; };

    explicit HotkeyModel(AppSettings *settings, QObject *parent = nullptr);

    QVariantList items() const;
    static QList<Action> defaultActions();

    Q_INVOKABLE QString shortcut(const QString &id) const;
    // Liefert die ID der kollidierenden Aktion (leer = kein Konflikt). force = trotzdem setzen (andere wird geleert)
    Q_INVOKABLE QString conflictFor(const QString &id, const QString &seq) const;
    Q_INVOKABLE bool setShortcut(const QString &id, const QString &seq, bool force = false);
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE QString labelFor(const QString &id) const;
    // Tastendruck aus QML (event.key, event.modifiers) → "Ctrl+Alt+X"; leer bei reinen Modifikatortasten
    Q_INVOKABLE QString sequenceFromKey(int key, int modifiers) const;

    void notifyLanguageChanged() { emit changed(); }
    // Tastenkürzel aktiver Plugins: [{id: "plugin:<id>:<eintrag>", label, category, defaultSeq}]. Ein Standardkürzel,
    // das schon belegt ist, wird nicht übernommen (der Nutzer kann es in den Einstellungen selbst setzen).
    void setExtraActions(const QVariantList &actions);

signals:
    void changed();

private:
    void save();
    static QString normalize(const QString &seq);
    AppSettings *m_settings;
    QList<Action> m_actions;
    QHash<QString, QString> m_overrides;
};

} // namespace uve
