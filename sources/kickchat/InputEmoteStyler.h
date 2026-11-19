#pragma once
// Gibt Emote-Namen im Eingabefeld genau die Breite ihres Bildes. Der Name bleibt als Text im Feld
// (Cursor, Rechtschreibung und Senden arbeiten weiter mit Zeichenpositionen), wird aber unsichtbar
// und über den Zeichenabstand auf Bildbreite gebracht. Das Bild legt das Panel darüber.

#include <QSyntaxHighlighter>
#include <QVariantList>
#include <QVector>

class InputEmoteStyler : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    // textEdit: das QML-TextEdit/TextArea; ranges: [{start,length,width}]
    static void apply(QObject *textEdit, const QVariantList &ranges);

protected:
    void highlightBlock(const QString &text) override;

private:
    explicit InputEmoteStyler(QTextDocument *document);

    struct Range { int start; int length; qreal width; };
    QVector<Range> m_ranges;
};
