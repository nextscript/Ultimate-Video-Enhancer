#pragma once
// Lokale Satzbauregeln: feste Muster, die ohne Netz und ohne Sprachmodell sicher erkennbar sind.
// Die eigentliche Grammatikprüfung übernimmt LanguageToolClient.

#include "SpellChecker.h"

#include <QVariantList>
#include <functional>

namespace GrammarRules {

// Fundstellen als [{start,length,message,messageEn,replacements,source:"local"}].
// words: Wörter aus SpellChecker::ruleWords. Wörter, an denen der Cursor steht, bleiben offen.
QVariantList check(const QString &text, const QList<SpellChecker::Word> &words, int cursor,
                   bool german, bool english,
                   const std::function<bool(const QString &)> &isGermanWord,
                   const std::function<bool(const QString &)> &isEnglishWord);

} // namespace GrammarRules
