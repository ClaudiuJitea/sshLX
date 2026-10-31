#include "highlightrules.h"

#include <QSettings>

QRegularExpression HighlightSet::compile(const HighlightRule &r)
{
    QString p = r.regex ? r.pattern : QRegularExpression::escape(r.pattern);
    if (r.wholeWord)
        p = QStringLiteral("(?<!\\w)(?:%1)(?!\\w)").arg(p);
    QRegularExpression::PatternOptions o = QRegularExpression::UseUnicodePropertiesOption;
    if (!r.matchCase)
        o |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(p, o);
}

QString HighlightSet::patternError(const HighlightRule &r)
{
    if (r.pattern.isEmpty())
        return QStringLiteral("empty pattern");
    QRegularExpression re = compile(r);
    return re.isValid() ? QString() : re.errorString();
}

void HighlightSet::setRules(const QList<HighlightRule> &rules)
{
    m_rules = rules;
    m_compiled.clear();
    for (int i = 0; i < rules.size(); i++) {
        if (!rules[i].enabled || !patternError(rules[i]).isEmpty())
            continue;
        QRegularExpression re = compile(rules[i]);
        re.optimize();
        m_compiled.append({i, re});
    }
}

QVector<int> HighlightSet::match(const QString &line) const
{
    QVector<int> owner(line.size(), -1);
    for (const Compiled &c : m_compiled) {
        for (auto it = c.re.globalMatch(line); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            for (int i = m.capturedStart(); i < m.capturedEnd(); i++)
                if (owner[i] < 0)
                    owner[i] = c.rule;
        }
    }
    return owner;
}

QList<HighlightRule> HighlightSet::load()
{
    QSettings st;
    QList<HighlightRule> rules;
    const int n = st.beginReadArray("highlight/rules");
    for (int i = 0; i < n; i++) {
        st.setArrayIndex(i);
        HighlightRule r;
        r.pattern = st.value("pattern").toString();
        r.regex = st.value("regex", false).toBool();
        r.matchCase = st.value("matchCase", false).toBool();
        r.wholeWord = st.value("wholeWord", true).toBool();
        r.bold = st.value("bold", true).toBool();
        r.enabled = st.value("enabled", true).toBool();
        r.fg = st.value("fg").toString();
        r.bg = st.value("bg").toString();
        rules.append(r);
    }
    st.endArray();
    return rules;
}

void HighlightSet::save(const QList<HighlightRule> &rules)
{
    QSettings st;
    st.remove("highlight/rules");
    st.beginWriteArray("highlight/rules", rules.size());
    for (int i = 0; i < rules.size(); i++) {
        const HighlightRule &r = rules[i];
        st.setArrayIndex(i);
        st.setValue("pattern", r.pattern);
        st.setValue("regex", r.regex);
        st.setValue("matchCase", r.matchCase);
        st.setValue("wholeWord", r.wholeWord);
        st.setValue("bold", r.bold);
        st.setValue("enabled", r.enabled);
        st.setValue("fg", r.fg);
        st.setValue("bg", r.bg);
    }
    st.endArray();
}

bool HighlightSet::enabled() { return QSettings().value("highlight/enabled", true).toBool(); }
void HighlightSet::setEnabled(bool on) { QSettings().setValue("highlight/enabled", on); }
