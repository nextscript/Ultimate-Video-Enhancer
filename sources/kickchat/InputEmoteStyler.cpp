#include "InputEmoteStyler.h"

#include <QFontMetricsF>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QVariantMap>

InputEmoteStyler::InputEmoteStyler(QTextDocument *document) : QSyntaxHighlighter(document) {}

void InputEmoteStyler::apply(QObject *textEdit, const QVariantList &ranges)
{
    // Das Dokument hängt direkt am TextEdit; so braucht die DLL kein Qt Quick.
    auto *document = textEdit ? textEdit->findChild<QTextDocument *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    if (!document) return;
    auto *styler = document->findChild<InputEmoteStyler *>(QString(), Qt::FindDirectChildrenOnly);
    if (!styler) styler = new InputEmoteStyler(document);

    styler->m_ranges.clear();
    for (const QVariant &v : ranges) {
        const QVariantMap r = v.toMap();
        styler->m_ranges.append({r.value(QStringLiteral("start")).toInt(), r.value(QStringLiteral("length")).toInt(),
                                 r.value(QStringLiteral("width")).toReal()});
    }
    styler->rehighlight();
}

void InputEmoteStyler::highlightBlock(const QString &text)
{
    const int blockStart = currentBlock().position();
    const QFontMetricsF metrics(document()->defaultFont());
    for (const Range &r : std::as_const(m_ranges)) {
        const int start = r.start - blockStart;
        // Noch die Bereiche zum vorigen Text: Das Panel schickt gleich die neuen.
        if (start < 0 || r.length <= 0 || start + r.length > text.size()) continue;
        QTextCharFormat format;
        format.setForeground(Qt::transparent);
        format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        format.setFontLetterSpacing((r.width - metrics.horizontalAdvance(text.mid(start, r.length))) / r.length);
        setFormat(start, r.length, format);
    }
}
