#include "highlightruledialog.h"

#include "dialogkit.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace DialogKit;

HighlightRuleDialog::HighlightRuleDialog(const HighlightRule &r, QWidget *parent)
    : QDialog(parent), m_fg(r.fg), m_bg(r.bg)
{
    setWindowTitle(tr("Keyword"));
    resize(520, 0);
    auto *v = new QVBoxLayout(this);
    frame(v);
    v->addWidget(header(tr("Keyword"), tr("Colours matching terminal text. Keywords are checked "
                                          "from the top of the list; the first match wins.")));
    m_pattern = new QLineEdit(r.pattern);
    m_regex = new QCheckBox(tr("Regular expression"));
    m_regex->setChecked(r.regex);
    m_case = new QCheckBox(tr("Match case"));
    m_case->setChecked(r.matchCase);
    m_word = new QCheckBox(tr("Whole word"));
    m_word->setChecked(r.wholeWord);
    m_bold = new QCheckBox(tr("Bold"));
    m_bold->setChecked(r.bold);
    m_fgButton = secondaryButton(QString());
    m_bgButton = secondaryButton(QString());
    m_preview = new QLabel;
    m_preview->setTextFormat(Qt::PlainText);
    m_preview->setContentsMargins(8, 4, 8, 4);

    auto *colours = new QHBoxLayout;
    colours->addWidget(m_fgButton);
    colours->addWidget(m_bgButton);
    colours->addStretch(1);
    auto *f = new QFormLayout;
    f->addRow(tr("Pattern"), m_pattern);
    f->addRow(QString(), m_regex);
    f->addRow(QString(), m_case);
    f->addRow(QString(), m_word);
    f->addRow(QString(), m_bold);
    f->addRow(tr("Colours"), colours);
    f->addRow(tr("Preview"), m_preview);
    v->addLayout(f);

    QPushButton *cancel = secondaryButton(tr("Cancel"));
    QPushButton *ok = primaryButton(tr("OK"));
    v->addWidget(footer({cancel, ok}));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, [this] {
        const QString problem = HighlightSet::patternError(rule());
        if (problem.isEmpty())
            accept();
        else
            error(this, tr("Keyword"), tr("Bad pattern: %1").arg(problem));
    });
    connect(m_fgButton, &QPushButton::clicked, this, [this] { pick(m_fg, tr("Text Colour")); });
    connect(m_bgButton, &QPushButton::clicked, this, [this] { pick(m_bg, tr("Background Colour")); });
    connect(m_pattern, &QLineEdit::textChanged, this, &HighlightRuleDialog::refresh);
    connect(m_bold, &QCheckBox::toggled, this, &HighlightRuleDialog::refresh);
    refresh();
}

HighlightRule HighlightRuleDialog::rule() const
{
    HighlightRule r;
    r.pattern = m_pattern->text();
    r.regex = m_regex->isChecked();
    r.matchCase = m_case->isChecked();
    r.wholeWord = m_word->isChecked();
    r.bold = m_bold->isChecked();
    r.fg = m_fg;
    r.bg = m_bg;
    return r;
}

// Cancelling the colour dialog clears the colour: that is how a colour is
// taken off again, since the terminal's own then shows through.
void HighlightRuleDialog::pick(QString &colour, const QString &title)
{
    QColor c = QColorDialog::getColor(colour.isEmpty() ? QColor(Qt::white) : QColor(colour),
                                      this, title);
    colour = c.isValid() ? c.name() : QString();
    refresh();
}

void HighlightRuleDialog::refresh()
{
    m_fgButton->setText(m_fg.isEmpty() ? tr("Text: default") : tr("Text: %1").arg(m_fg));
    m_bgButton->setText(m_bg.isEmpty() ? tr("Background: none") : tr("Background: %1").arg(m_bg));
    m_preview->setText(m_pattern->text().isEmpty() ? tr("keyword") : m_pattern->text());
    m_preview->setStyleSheet(QStringLiteral("background: %1; color: %2; font-family: monospace; "
                                            "font-weight: %3;")
                                 .arg(m_bg.isEmpty() ? QStringLiteral("#000000") : m_bg,
                                      m_fg.isEmpty() ? QStringLiteral("#bbbbbb") : m_fg,
                                      m_bold->isChecked() ? QStringLiteral("bold")
                                                          : QStringLiteral("normal")));
}
