#include "Translation.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <algorithm>

namespace uve {

LanguageManager::LanguageManager(const QStringList &searchDirs, QObject *parent) : QObject(parent), m_dirs(searchDirs) {}

QHash<QString, QString> LanguageManager::scan() const
{
    QHash<QString, QString> out;
    static const QRegularExpression re(QStringLiteral("^uve_([a-z]{2,3}(?:_[A-Z]{2})?)\\.qm$"));
    for (const auto &dir : m_dirs) {                       // spätere Ordner (Benutzer) überschreiben frühere
        for (const auto &fi : QDir(dir).entryInfoList({QStringLiteral("uve_*.qm")}, QDir::Files, QDir::Name)) {
            const auto m = re.match(fi.fileName());
            if (!m.hasMatch() || m.captured(1) == QLatin1String(kSourceLanguage)) continue;
            if (fi.size() < 16) continue;                  // leere/defekte Datei
            out.insert(m.captured(1), fi.absoluteFilePath());
        }
    }
    return out;
}

static QString capitalized(QString s) { if (!s.isEmpty()) s[0] = s[0].toUpper(); return s; }

QVariantList LanguageManager::available() const
{
    struct Row { QString code, name, nativeName, file; };
    QList<Row> rows;
    const auto found = scan();
    for (auto it = found.begin(); it != found.end(); ++it) {
        const QLocale loc(it.key());
        const bool known = loc.language() != QLocale::C;
        rows << Row{it.key(), known ? QLocale::languageToString(loc.language()) : it.key(),
                    known ? capitalized(loc.nativeLanguageName()) : it.key(), QDir::toNativeSeparators(it.value())};
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.nativeName.localeAwareCompare(b.nativeName) < 0; });
    QVariantList out;
    out << QVariantMap{{"code", kSourceLanguage}, {"name", "German"}, {"nativeName", "Deutsch"}, {"builtin", true}};
    for (const auto &r : rows)
        out << QVariantMap{{"code", r.code}, {"name", r.name}, {"nativeName", r.nativeName}, {"file", r.file}, {"builtin", false}};
    return out;
}

bool LanguageManager::apply(const QString &code, QString *error)
{
    if (m_primary) QCoreApplication::removeTranslator(m_primary.get());
    if (m_fallback) QCoreApplication::removeTranslator(m_fallback.get());
    m_primary.reset();
    m_fallback.reset();
    const auto langs = scan();
    if (code != QLatin1String(kSourceLanguage) && !langs.contains(code)) {
        if (error) *error = QStringLiteral("Sprachdatei uve_%1.qm nicht gefunden").arg(code);
        m_current = QString::fromLatin1(kSourceLanguage);
        return false;
    }
    bool ok = true;
    if (code != QLatin1String(kSourceLanguage)) {
        // Rückfall Englisch zuerst installieren: Qt fragt zuletzt installierte Übersetzer zuerst
        const QString en = QString::fromLatin1(kDefaultLanguage);
        if (code != en && langs.contains(en)) {
            m_fallback = std::make_unique<QTranslator>();
            if (m_fallback->load(langs.value(en))) QCoreApplication::installTranslator(m_fallback.get());
            else m_fallback.reset();
        }
        m_primary = std::make_unique<QTranslator>();
        if (m_primary->load(langs.value(code))) {
            QCoreApplication::installTranslator(m_primary.get());
        } else {
            ok = false;
            if (error) *error = QStringLiteral("Sprachdatei fehlerhaft: ") + langs.value(code);
            Logger::write("i18n", "Sprachdatei fehlerhaft: " + langs.value(code));
            m_primary.reset();
        }
    }
    m_current = code;
    Logger::write("i18n", QStringLiteral("Sprache: %1%2").arg(code, m_primary ? " (" + m_primary->filePath() + ")" : QString()));
    return ok;
}

} // namespace uve
