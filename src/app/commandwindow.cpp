#include "commandwindow.h"

#include <QButtonGroup>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include "dialogkit.h"
#include "documentarea.h"
#include "icons.h"
#include "sessionwidget.h"
#include "tabcolours.h"

static constexpr int kMaxInputLines = 5;

static QToolButton *pill(const QString &text, const char *kind = "true")
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setCheckable(true);
    b->setProperty("pill", kind);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

CommandWindow::CommandWindow(DocumentArea *docs, QWidget *parent) : QWidget(parent), m_docs(docs)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(8, 4, 8, 6);
    v->setSpacing(6);

    m_pillRow = new QHBoxLayout;
    m_pillRow->setSpacing(4);
    m_pills = new QButtonGroup(this);
    m_pills->setExclusive(true);
    const QList<QPair<Target, QString>> fixed{{Current, tr("Current")}, {Visible, tr("Visible")},
                                              {All, tr("All")}, {Chosen, tr("Choose")}};
    for (const auto &[target, text] : fixed) {
        QToolButton *b = pill(text);
        m_pills->addButton(b, target);
        m_pillRow->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this, target = target] { setTarget(target); });
    }
    m_pills->button(Current)->setChecked(true);
    m_summary = new QLabel;
    m_summary->setObjectName("commandSummary");
    m_live = pill(tr("Type live"), "live");
    m_live->setObjectName("liveTyping");
    m_live->setToolTip(tr("Each key typed in a terminal also goes to the other tabs selected here."));
    m_live->setAccessibleDescription(m_live->toolTip());
    m_pillRow->addStretch(1);
    m_pillRow->addWidget(m_summary);
    m_pillRow->addSpacing(6);
    m_pillRow->addWidget(m_live);
    v->addLayout(m_pillRow);

    // The tabs themselves, one click each, shown only when choosing.
    m_chipHost = new QWidget;
    m_chipRow = new QHBoxLayout(m_chipHost);
    m_chipRow->setContentsMargins(0, 0, 0, 0);
    m_chipRow->setSpacing(4);
    m_chipArea = new QScrollArea;
    m_chipArea->setObjectName("commandChips");
    m_chipArea->setFrameShape(QFrame::NoFrame);
    m_chipArea->setWidgetResizable(true);
    m_chipArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chipArea->setWidget(m_chipHost);
    m_chipArea->hide();
    v->addWidget(m_chipArea);

    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    m_input = new QPlainTextEdit;
    m_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_input->setPlaceholderText(tr("Command to send.  Enter sends, Shift+Enter adds a line, "
                                   "Up recalls."));
    m_input->setAccessibleName(tr("Command to send"));
    m_input->installEventFilter(this);
    m_send = DialogKit::primaryButton(tr("Send"));
    m_send->setIcon(Icons::get("send"));
    row->addWidget(m_input, 1);
    row->addWidget(m_send, 0, Qt::AlignBottom);
    v->addLayout(row);
    fitInput();

    connect(m_send, &QPushButton::clicked, this, &CommandWindow::sendInput);
    connect(m_input, &QPlainTextEdit::textChanged, this, &CommandWindow::fitInput);
    connect(m_live, &QToolButton::toggled, this, &CommandWindow::updateSummary);
    SessionWidget::setMirror([this](SessionWidget *source) {
        QList<SessionWidget *> list;
        if (m_live->isChecked())
            list = targets();
        list.removeAll(source);
        return list;
    });
    connect(docs, &DocumentArea::sessionCountChanged, this, &CommandWindow::refreshGroups);
    connect(docs, &DocumentArea::currentSessionChanged, this, &CommandWindow::updateSummary);
    // Always starts on the current tab: a send to many should be chosen each time.
    updateSummary();
}

void CommandWindow::fitInput()
{
    const int blocks = m_input->document()->blockCount();
    const int lines = qBound(1, blocks, kMaxInputLines);
    m_input->setVerticalScrollBarPolicy(blocks > kMaxInputLines ? Qt::ScrollBarAsNeeded
                                                                : Qt::ScrollBarAlwaysOff);
    m_input->setFixedHeight(m_input->fontMetrics().lineSpacing() * lines + 14);
}

void CommandWindow::setTarget(Target t)
{
    m_target = t;
    m_group.clear();
    m_pills->button(t)->setChecked(true);
    m_chipArea->setVisible(t == Chosen);
    if (t == Chosen)
        rebuildChips();
    updateSummary();
}

void CommandWindow::setTargetGroup(const QString &name)
{
    m_group = name;
    m_chipArea->hide();
    refreshGroups();
}

void CommandWindow::setChosen(const QList<SessionWidget *> &tabs)
{
    m_chosen.clear();
    for (SessionWidget *s : tabs)
        m_chosen.append(s);
    rebuildChips();
    updateSummary();
}

void CommandWindow::refreshGroups()
{
    rebuildGroups();
    rebuildChips();
    updateSummary();
}

void CommandWindow::rebuildGroups()
{
    const QStringList names = m_docs->groupNames();
    if (!m_group.isEmpty() && !names.contains(m_group)) {
        m_group.clear();          // its last tab went
        m_target = Current;
        m_pills->button(Current)->setChecked(true);
    }
    for (QToolButton *b : std::as_const(m_groupPills)) {
        m_pills->removeButton(b);
        delete b;
    }
    m_groupPills.clear();
    int at = 4;                   // after the four fixed pills
    for (const QString &n : names) {
        QToolButton *b = pill(n, "group");
        b->setToolTip(tr("Tab group %1").arg(n));
        b->setStyleSheet(TabColours::pillStyle(TabColours::groupColour(n)));
        m_pills->addButton(b);
        m_pillRow->insertWidget(at++, b);
        m_groupPills.append(b);
        b->setChecked(n == m_group);
        connect(b, &QToolButton::clicked, this, [this, n] {
            m_group = n;
            m_chipArea->hide();
            updateSummary();
        });
    }
}

void CommandWindow::rebuildChips()
{
    while (QLayoutItem *item = m_chipRow->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_chosen.removeAll(nullptr);
    QHash<QString, int> uses, seen;
    QList<SessionWidget *> tabs;
    for (SessionWidget *s : m_docs->sessions()) {
        if (!s->isFileTransfer()) {
            tabs.append(s);
            uses[s->title()]++;
        }
    }
    for (SessionWidget *s : tabs) {
        const QString title = s->title();
        // Two tabs on one host share a title, and must still be told apart.
        const QString label = uses[title] > 1 ? tr("%1 (%2)").arg(title).arg(++seen[title]) : title;
        QToolButton *b = pill(fontMetrics().elidedText(label, Qt::ElideMiddle, 240), "chip");
        b->setToolTip(label);
        b->setProperty("fullLabel", label);
        b->setStyleSheet(TabColours::pillStyle(m_docs->colourFor(s)));
        b->setChecked(m_chosen.contains(s));
        QPointer<SessionWidget> p(s);
        connect(b, &QToolButton::toggled, this, [this, p](bool on) {
            m_chosen.removeAll(p);
            if (on && p)
                m_chosen.append(p);
            updateSummary();
        });
        m_chipRow->addWidget(b);
    }
    if (tabs.isEmpty())
        m_chipRow->addWidget(new QLabel(tr("No terminal tabs are open.")));
    m_chipRow->addStretch(1);
    // Not sizeHint(): new chips aren't styled yet, and it comes out far too small.
    m_chipArea->setFixedHeight(fontMetrics().height() + 16);
}

QStringList CommandWindow::chipLabels() const
{
    QStringList out;
    for (QToolButton *b : m_chipHost->findChildren<QToolButton *>())
        out.append(b->property("fullLabel").toString());
    return out;
}

void CommandWindow::setLive(bool on)
{
    m_live->setChecked(on);
}

bool CommandWindow::live() const
{
    return m_live->isChecked();
}

void CommandWindow::hideEvent(QHideEvent *e)
{
    // Nobody should be typing into other tabs without seeing that they are.
    m_live->setChecked(false);
    QWidget::hideEvent(e);
}

QList<SessionWidget *> CommandWindow::targets() const
{
    QList<SessionWidget *> r;
    if (!m_group.isEmpty()) {
        for (SessionWidget *s : m_docs->sessions())
            if (s->group() == m_group)
                r.append(s);
    } else if (m_target == Current) {
        if (SessionWidget *s = m_docs->currentSession())
            r.append(s);
    } else {
        for (SessionWidget *s : m_docs->sessions())
            if (m_target == All || (m_target == Visible ? s->isVisible() : m_chosen.contains(s)))
                r.append(s);
    }
    r.erase(std::remove_if(r.begin(), r.end(), [](SessionWidget *s) { return s->isFileTransfer(); }),
            r.end());
    return r;
}

void CommandWindow::updateSummary()
{
    const QList<SessionWidget *> list = targets();
    const int n = int(list.size());
    QString text;
    if (m_live->isChecked())
        text = tr("Typing goes to %n tab(s)", nullptr, n);
    else if (n == 0)
        text = tr("No tabs selected");
    else if (m_target == Current && m_group.isEmpty())
        text = list.first()->title();
    else
        text = tr("%n tab(s)", nullptr, n);
    m_summary->setText(text);
    QFont f = m_summary->font();
    f.setBold(n > 1 || m_live->isChecked());
    m_summary->setFont(f);
    m_send->setEnabled(n > 0);
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
