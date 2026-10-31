// HighlightRuleDialog - edits one keyword of the highlight list.
#pragma once

#include <QDialog>

#include "highlightrules.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

class HighlightRuleDialog : public QDialog {
    Q_OBJECT
public:
    HighlightRuleDialog(const HighlightRule &rule, QWidget *parent);
    HighlightRule rule() const;

private:
    void pick(QString &colour, const QString &title);
    void refresh();

    QLineEdit *m_pattern;
    QCheckBox *m_regex, *m_case, *m_word, *m_bold;
    QPushButton *m_fgButton, *m_bgButton;
    QLabel *m_preview;
    QString m_fg, m_bg;
};
