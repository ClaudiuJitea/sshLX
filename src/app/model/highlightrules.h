// HighlightRules - the user's keyword list: words or regular expressions,
// each with its own colours, checked from the top so the first rule to
// claim a character wins. A generic pattern therefore goes below the
// specific ones. Qt Core only; colours are "#rrggbb" or empty for "leave".
#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QVector>

struct HighlightRule {
    QString pattern;
    bool regex = false;
    bool matchCase = false;
    bool wholeWord = true;
    bool bold = true;
    bool enabled = true;
    QString fg = QStringLiteral("#ff5555");
    QString bg;
};

class HighlightSet {
public:
    void setRules(const QList<HighlightRule> &rules);
    const QList<HighlightRule> &rules() const { return m_rules; }
    bool isEmpty() const { return m_compiled.isEmpty(); }
    // For each UTF-16 unit of line, the index into rules() that owns it, or -1.
    QVector<int> match(const QString &line) const;

    static QRegularExpression compile(const HighlightRule &rule);
    // The pattern's complaint, or empty if it compiles.
    static QString patternError(const HighlightRule &rule);
    static QList<HighlightRule> load();
    static void save(const QList<HighlightRule> &rules);
    static bool enabled();
    static void setEnabled(bool on);

private:
    QList<HighlightRule> m_rules;
    struct Compiled { int rule; QRegularExpression re; };
    QList<Compiled> m_compiled;
};
