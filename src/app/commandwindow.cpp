#include "commandwindow.h"

#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "documentarea.h"
#include "icons.h"
#include "sessionwidget.h"

CommandWindow::CommandWindow(DocumentArea *docs, QWidget *parent) : QWidget(parent), m_docs(docs)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(6, 4, 6, 4);
    v->setSpacing(4);
    auto *row = new QHBoxLayout;
    m_target = new QComboBox;
    m_target->addItem(tr("Current tab"), Current);
    m_target->addItem(tr("Visible tabs"), Visible);
    m_target->addItem(tr("All tabs"), All);
    m_target->addItem(tr("Chosen tabs"), Chosen);
    m_choose = new QToolButton;
    m_choose->setText(tr("Choose..."));
    m_choose->setPopupMode(QToolButton::InstantPopup);
    m_choose->setMenu(new QMenu(m_choose));
    connect(m_choose->menu(), &QMenu::aboutToShow, this, &CommandWindow::buildChooser);
    m_summary = new QLabel;
    m_send = DialogKit::primaryButton(tr("Send"));
    m_send->setIcon(Icons::get("send"));
    row->addWidget(new QLabel(tr("Send to")));
    row->addWidget(m_target);
    row->addWidget(m_choose);
    row->addWidget(m_summary, 1);
    row->addWidget(m_send);
    v->addLayout(row);
    m_input = new QPlainTextEdit;
    m_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_input->setPlaceholderText(tr("Enter sends. Shift+Enter starts a new line. Up and Down recall "
                                   "earlier commands."));
    m_input->setMaximumHeight(m_input->fontMetrics().lineSpacing() * 3 + 12);
    m_input->installEventFilter(this);
    v->addWidget(m_input);

    connect(m_send, &QPushButton::clicked, this, &CommandWindow::sendInput);
    connect(m_target, &QComboBox::currentIndexChanged, this, [this] {
        updateSummary();
    });
    connect(docs, &DocumentArea::sessionCountChanged, this, &CommandWindow::updateSummary);
    connect(docs, &DocumentArea::currentSessionChanged, this, &CommandWindow::updateSummary);
    // Always starts on the current tab: a send to many should be chosen each time.
    updateSummary();
}

void CommandWindow::setTarget(Target t)
{
    m_target->setCurrentIndex(m_target->findData(t));
}

void CommandWindow::setChosen(const QList<SessionWidget *> &tabs)
{
    m_chosen.clear();
    for (SessionWidget *s : tabs)
        m_chosen.append(s);
    updateSummary();
}

void CommandWindow::buildChooser()
{
    QMenu *menu = m_choose->menu();
    menu->clear();
    const QList<SessionWidget *> all = m_docs->sessions();
    for (SessionWidget *s : all) {
        if (s->isFileTransfer())
            continue;
        QAction *a = menu->addAction(s->title());
        a->setCheckable(true);
        a->setChecked(m_chosen.contains(s));
        QPointer<SessionWidget> p(s);
        connect(a, &QAction::toggled, this, [this, p](bool on) {
            m_chosen.removeAll(p);
            if (on && p)
                m_chosen.append(p);
            updateSummary();
        });
    }
    if (menu->isEmpty())
        menu->addAction(tr("(no terminal tabs)"))->setEnabled(false);
}

QList<SessionWidget *> CommandWindow::targets() const
{
    QList<SessionWidget *> r;
    switch (m_target->currentData().toInt()) {
    case Current:
        if (SessionWidget *s = m_docs->currentSession())
            r.append(s);
        break;
    case Visible:
        for (SessionWidget *s : m_docs->sessions())
            if (s->isVisible())
                r.append(s);
        break;
    case All:
        r = m_docs->sessions();
        break;
    default:
        for (SessionWidget *s : m_docs->sessions())
            if (m_chosen.contains(s))
                r.append(s);
    }
    r.erase(std::remove_if(r.begin(), r.end(), [](SessionWidget *s) { return s->isFileTransfer(); }),
            r.end());
    return r;
}

void CommandWindow::updateSummary()
{
    const int n = int(targets().size());
    const bool wide = m_target->currentData().toInt() != Current;
    m_choose->setVisible(m_target->currentData().toInt() == Chosen);
    m_summary->setText(wide ? tr("%n tab(s)", nullptr, n) : QString());
    QFont f = m_summary->font();
    f.setBold(wide && n > 1);
    m_summary->setFont(f);
}

int CommandWindow::send(const QString &text)
{
    QStringList lines = text.split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty())
        return 0;
    const QList<SessionWidget *> list = targets();
    int sent = 0;
    for (SessionWidget *s : list) {
        if (!s->canSendCommands())
            continue;
        if (lines.size() > 1)
            s->sendCommands(lines);
        else
            s->sendCommand(lines.first());
        sent++;
    }
    const int skipped = int(list.size()) - sent;
    QString note = tr("Sent to %n tab(s)", nullptr, sent);
    if (skipped)
        note += tr(", %n busy or disconnected", nullptr, skipped);
    m_summary->setText(note);
    return sent;
}

void CommandWindow::sendInput()
{
    const QString text = m_input->toPlainText();
    if (text.trimmed().isEmpty() && !text.isEmpty())
        return;
    // Enter on an empty box sends just Enter, as at a prompt.
    if (text.isEmpty()) {
        for (SessionWidget *s : targets())
            s->sendCommand(QString());
        return;
    }
    if (send(text) > 0) {
        m_history.removeAll(text);
        m_history.append(text);
        m_historyPos = int(m_history.size());
        m_input->clear();
    }
}

void CommandWindow::focusInput()
{
    m_input->setFocus();
}

void CommandWindow::history(int delta)
{
    const int pos = m_historyPos + delta;
    if (pos < 0 || pos > m_history.size())
        return;
    m_historyPos = pos;
    m_input->setPlainText(pos < m_history.size() ? m_history[pos] : QString());
    m_input->moveCursor(QTextCursor::End);
}

bool CommandWindow::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_input && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)
            && !(k->modifiers() & Qt::ShiftModifier)) {
            sendInput();
            return true;
        }
        const QTextCursor c = m_input->textCursor();
        if (k->key() == Qt::Key_Up && c.block() == m_input->document()->firstBlock()) {
            history(-1);
            return true;
        }
        if (k->key() == Qt::Key_Down && c.block() == m_input->document()->lastBlock()) {
            history(+1);
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}
