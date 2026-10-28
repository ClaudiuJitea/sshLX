#include "eventlogdialog.h"

#include "dialogkit.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

EventLogDialog::EventLogDialog(const QString &title, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    resize(760, 460);
    auto *v = new QVBoxLayout(this);
    DialogKit::frame(v);
    v->addWidget(DialogKit::header(title, tr("Connection events, as in PuTTY's Event Log. "
                                             "Select lines to copy them.")));
    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    v->addWidget(m_list);
    QPushButton *copy = DialogKit::secondaryButton(tr("&Copy"));
    QPushButton *close = DialogKit::primaryButton(tr("Close"));
    v->addWidget(DialogKit::footer({copy, close}));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(copy, &QPushButton::clicked, this, [this] {
        QStringList lines;
        for (int i = 0; i < m_list->count(); i++)
            if (m_list->item(i)->isSelected())
                lines << m_list->item(i)->text();
        // As PuTTY: copy selected lines, or everything if none selected.
        if (lines.isEmpty())
            for (int i = 0; i < m_list->count(); i++)
                lines << m_list->item(i)->text();
        QApplication::clipboard()->setText(lines.join('\n') + '\n');
    });
}

void EventLogDialog::appendLine(const QString &line)
{
    bool atEnd = m_list->count() == 0 ||
        m_list->currentRow() == m_list->count() - 1 ||
        m_list->currentRow() < 0;
    m_list->addItem(QString(line).replace('\t', "  "));
    if (atEnd)
        m_list->scrollToBottom();
}
