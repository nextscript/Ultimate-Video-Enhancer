#pragma once
// Sprachen als Qt-Übersetzungsdateien (.qm) im Ordner "lang" (siehe lang/README.md):
//
//   lang/uve_<code>.ts   Quelle (Qt-Linguist-Format, bearbeitbar mit Qt Linguist)
//   lang/uve_<code>.qm   kompiliert (lrelease, beim Build automatisch) – wird vom Player geladen
//
// Quellsprache der Texte ist Deutsch. Fehlende Übersetzungen fallen auf Englisch und danach auf Deutsch zurück.
// Gesucht wird in <Programmordner>/lang und <Datenordner>/lang (Benutzerdateien haben Vorrang).
#include <QObject>
#include <QStringList>
#include <QTranslator>
#include <QVariantList>
#include <memory>

namespace uve {

class LanguageManager : public QObject
{
    Q_OBJECT
public:
    static constexpr const char *kSourceLanguage = "de";   // Texte im Quellcode
    static constexpr const char *kDefaultLanguage = "en";  // Voreinstellung

    explicit LanguageManager(const QStringList &searchDirs, QObject *parent = nullptr);

    // [{code, name, nativeName, file, builtin}] – Deutsch (Quelltexte) ist immer enthalten
    QVariantList available() const;
    bool apply(const QString &code, QString *error = nullptr);
    QString current() const { return m_current; }
    QStringList searchDirs() const { return m_dirs; }

private:
    QHash<QString, QString> scan() const;   // code → .qm-Datei

    QStringList m_dirs;
    QString m_current;
    std::unique_ptr<QTranslator> m_primary, m_fallback;
};

} // namespace uve
