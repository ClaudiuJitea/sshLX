#include "findbar.h"

#include "dialogkit.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

FindBar::FindBar(QWidget *parent) : QFrame(parent)
{
    setObjectName("PromptBar");
    setFrameShape(QFrame::StyledPanel);
    auto *h = new QHBoxLayout(this);
    m_edit = new QLineEdit;
    m_edit->setPlaceholderText(tr("Find in terminal"));
    m_case = new QCheckBox(tr("Match case"));
    m_result = new QLabel;
    m_result->setMinimumWidth(100);
    m_result->setAlignment(Qt::AlignCenter);
    auto *prev = DialogKit::secondaryButton(tr("Previous"));
    auto *nextButton = DialogKit::secondaryButton(tr("Next"));
    auto *close = DialogKit::secondaryButton(tr("Close"));
    h->addWidget(m_edit, 1);
    h->addWidget(m_case);
    h->addWidget(m_result);
    h->addWidget(prev);
    h->addWidget(nextButton);
    h->addWidget(close);
    connect(m_edit, &QLineEdit::textChanged, this, &FindBar::queryChanged);
    connect(m_case, &QCheckBox::toggled, this, &FindBar::queryChanged);
    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
            emit next();
        else
            emit previous();     // Enter walks up, away from the newest output
    });
    connect(prev, &QPushButton::clicked, this, &FindBar::previous);
    connect(nextButton, &QPushButton::clicked, this, &FindBar::next);
    connect(close, &QPushButton::clicked, this, &FindBar::closed);
    hide();
}

void FindBar::open()
{
    show();
    m_edit->setFocus();
    m_edit->selectAll();
}

QString FindBar::text() const { return m_edit->text(); }
bool FindBar::matchCase() const { return m_case->isChecked(); }
void FindBar::setResult(const QString &text) { m_result->setText(text); }

void FindBar::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
        emit closed();
    else
        QFrame::keyPressEvent(e);
}
