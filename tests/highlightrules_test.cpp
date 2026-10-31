// The keyword list's matching rules.
#include <QtTest>

#include "highlightrules.h"

static HighlightRule word(const char *p)
{
    HighlightRule r;
    r.pattern = p;
    return r;
}

static HighlightRule regex(const char *p)
{
    HighlightRule r = word(p);
    r.regex = true;
    r.wholeWord = false;
    return r;
}

// One digit per character: the owning rule, or '.'.
static QString owners(const HighlightSet &s, const QString &line)
{
    QString out;
    for (int o : s.match(line))
        out += o < 0 ? QChar('.') : QChar('0' + o);
    return out;
}

class HighlightRulesTest : public QObject {
    Q_OBJECT
private slots:
    void wholeWordAndCase()
    {
        HighlightSet s;
        s.setRules({word("up")});
        QCOMPARE(owners(s, "up Up upper setup"), QString("00.00............"));
        HighlightRule strict = word("up");
        strict.matchCase = true;
        s.setRules({strict});
        QCOMPARE(owners(s, "up Up"), QString("00..."));
    }
    void literalIsNotARegex()
    {
        HighlightSet s;
        HighlightRule r = word("a.b");
        r.wholeWord = false;
        s.setRules({r});
        QCOMPARE(owners(s, "axb a.b"), QString("....000"));
    }
    void firstRuleWins()
    {
        // The video's case: config prompts before the generic # prompt.
        HighlightSet s;
        s.setRules({regex(R"([^#]+\(config\)#)"), regex("[^#]+#")});
        QCOMPARE(owners(s, "R1(config)#"), QString("00000000000"));
        QCOMPARE(owners(s, "R1#"), QString("111"));
    }
    void disabledAndBrokenRulesAreLeftOut()
    {
        HighlightRule off = word("up");
        off.enabled = false;
        HighlightSet s;
        s.setRules({off, regex("(")});
        QVERIFY(s.isEmpty());
        QVERIFY(!HighlightSet::patternError(regex("(")).isEmpty());
        QCOMPARE(HighlightSet::patternError(regex("a+")), QString());
    }
};

QTEST_GUILESS_MAIN(HighlightRulesTest)
#include "highlightrules_test.moc"
