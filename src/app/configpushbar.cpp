#include "configpushbar.h"

#include "dialogkit.h"
#include "icons.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

ConfigPushBar::ConfigPushBar(ConfigPush *push, QWidget *parent)
    : QFrame(parent), m_push(push)
{
    setObjectName("PromptBar");
    setFrameShape(QFrame::StyledPanel);
    auto *v = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setMinimumWidth(120);
    m_bar = new QProgressBar;
    m_bar->setFixedWidth(160);
    m_bar->setTextVisible(false);
    m_bar->setMaximumHeight(10);
    m_pause = DialogKit::secondaryButton(tr("Pause"));
    m_stop = DialogKit::dangerButton(tr("Stop"));
    m_copy = DialogKit::secondaryButton(tr("Copy Report"));
    m_close = DialogKit::primaryButton(tr("Close"));
    m_lines = DialogKit::secondaryButton(tr("Lines"));
    m_lines->setCheckable(true);
    m_again = DialogKit::primaryButton(tr("Retry Failed"));
    top->addWidget(m_status, 1);
    top->addWidget(m_bar);
    top->addWidget(m_lines);
    top->addWidget(m_again);
    top->addWidget(m_pause);
    top->addWidget(m_stop);
    top->addWidget(m_copy);
    top->addWidget(m_close);
    v->addLayout(top);

    // Every line and how it went.
    m_list = new QListWidget;
    m_list->setObjectName("pushLines");
    m_list->setAccessibleName(tr("Configuration lines"));
    m_list->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_list->setMaximumHeight(130);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->hide();
    v->addWidget(m_list);
    connect(m_lines, &QPushButton::toggled, this, [this](bool on) {
        m_list->setVisible(on);
        if (on)
            updateList();
    });
    connect(m_again, &QPushButton::clicked, this, [this] {
        m_again->hide();
        m_push->retryFailed();
    });

    m_ask = new QWidget;
    auto *ask = new QVBoxLayout(m_ask);
    ask->setContentsMargins(0, 0, 0, 0);
    m_question = new QLabel;
    m_question->setTextFormat(Qt::PlainText);
    m_question->setWordWrap(true);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setFont(mono);
    m_output->setMaximumHeight(90);
    m_edit = new QLineEdit;
    m_edit->setFont(mono);
    auto *row = new QHBoxLayout;
    m_send = DialogKit::primaryButton(tr("Send"));
    m_saved = DialogKit::secondaryButton(tr("Saved Password"));
    auto *savedMenu = new QMenu(m_saved);
    m_saved->setMenu(savedMenu);
    connect(savedMenu, &QMenu::aboutToShow, this, [this, savedMenu] {
        savedMenu->clear();
        emit passwordMenuRequested(savedMenu);
    });
    m_skip = DialogKit::secondaryButton(tr("Skip Line"));
    m_wait = DialogKit::secondaryButton(tr("Keep Waiting"));
    m_ignore = DialogKit::secondaryButton(tr("Ignore Further Errors"));
    row->addWidget(m_edit, 1);
    row->addWidget(m_send);
    row->addWidget(m_saved);
    row->addWidget(m_skip);
    row->addWidget(m_wait);
    row->addWidget(m_ignore);
    ask->addWidget(m_question);
    ask->addWidget(m_output);
    ask->addLayout(row);
    v->addWidget(m_ask);

    connect(m_push, &ConfigPush::progress, this, &ConfigPushBar::onProgress);
    connect(m_push, &ConfigPush::paused, this, &ConfigPushBar::onPaused);
    connect(m_push, &ConfigPush::finished, this, &ConfigPushBar::onFinished);
    connect(m_pause, &QPushButton::clicked, this, [this] {
        if (m_reason == ConfigPush::Pause::ByUser) {
            showRunning();
            m_push->resume();
        } else {
            m_pause->setEnabled(false);
            m_status->setText(tr("Pausing after this line..."));
            m_push->requestPause();
        }
    });
    connect(m_stop, &QPushButton::clicked, m_push, &ConfigPush::stop);
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_push->report() + '\n');
    });
    connect(m_close, &QPushButton::clicked, this, [this] {
        hide();
        emit closed();
    });
    connect(m_edit, &QLineEdit::returnPressed, this, &ConfigPushBar::sendEdited);
    connect(m_edit, &QLineEdit::textChanged, this, &ConfigPushBar::updateSendLabel);
    connect(m_send, &QPushButton::clicked, this, &ConfigPushBar::sendEdited);
    connect(m_skip, &QPushButton::clicked, this, [this] {
        showRunning();
        m_push->skip();
    });
    connect(m_wait, &QPushButton::clicked, this, [this] {
        // At a question the answer is typed in the terminal, and the push
        // goes on by itself when the device's prompt comes back.
        if (m_reason == ConfigPush::Pause::Question) {
            emit closed();
            return;
        }
        showRunning();
        m_push->keepWaiting();
    });
    connect(m_push, &ConfigPush::resumed, this, &ConfigPushBar::showRunning);
    connect(m_ignore, &QPushButton::clicked, this, [this] {
        showRunning();
        m_push->ignoreErrors();
    });
    hide();
}

void ConfigPushBar::showRunning()
{
    m_reason = ConfigPush::Pause::None;
    m_ask->hide();
    m_copy->hide();
    m_close->hide();
    m_again->hide();
    m_bar->show();
    m_pause->show();
    m_pause->setEnabled(true);
    m_pause->setText(tr("Pause"));
    m_stop->show();
    show();
    onProgress(m_push->current(), m_push->total());
    emit inputBlockChanged(true);
}

void ConfigPushBar::onProgress(int current, int total)
{
    if (isHidden() && m_push->running()) {
        showRunning();
        return;
    }
    m_bar->setRange(0, qMax(total, 1));
    m_bar->setValue(current);
    updateList();
    if (m_reason != ConfigPush::Pause::None || !m_pause->isEnabled())
        return;
    const QList<ConfigPush::Line> lines = m_push->lines();
    m_status->setText(current < lines.size()
        ? tr("Line %1 of %2: %3").arg(current + 1).arg(total).arg(lines[current].text.trimmed())
        : tr("Line %1 of %2").arg(current).arg(total));
}

void ConfigPushBar::onPaused(ConfigPush::Pause reason, const QString &output)
{
    using Pause = ConfigPush::Pause;
    m_reason = reason;
    emit inputBlockChanged(false);
    const QList<ConfigPush::Line> lines = m_push->lines();
    const int n = m_push->current();
    const QString line = n < lines.size() ? lines[n].text : QString();
    m_status->setText(tr("Paused at line %1 of %2").arg(n + 1).arg(lines.size()));
    m_pause->setEnabled(true);
    m_pause->setText(tr("Resume"));
    m_pause->setVisible(reason == Pause::ByUser);
    m_ask->setVisible(reason != Pause::ByUser);
    m_output->setVisible(!output.isEmpty());
    m_output->setPlainText(output);
    m_edit->setVisible(reason != Pause::NoPrompt);
    m_edit->setText(reason == Pause::Question ? QString() : line);
    const bool secret = reason == Pause::Question && ConfigPush::looksLikePasswordPrompt(output);
    m_edit->setEchoMode(secret ? QLineEdit::Password : QLineEdit::Normal);
    m_saved->setVisible(reason == Pause::Question);
    m_wait->setVisible(reason == Pause::Timeout || reason == Pause::Question);
    m_wait->setText(reason == Pause::Question ? tr("Answer in Terminal") : tr("Keep Waiting"));
    // Moving on from a question would type the next line as its answer.
    m_skip->setVisible(reason != Pause::Question);
    updateList();
    m_ignore->setVisible(reason == Pause::DeviceError);
    m_skip->setText(reason == Pause::NoPrompt ? tr("Start Anyway") : tr("Skip Line"));
    switch (reason) {
    case Pause::DeviceError:
        m_question->setText(tr("The device rejected this line. Amend and send it, or skip it "
                               "and continue. You can also fix it in the terminal."));
        break;
    case Pause::Timeout:
        m_question->setText(tr("This line has not finished: the device's prompt has not come "
                               "back. Skip Line waits for it before sending the next one."));
        break;
    case Pause::Question:
        m_question->setText(tr("The device is asking something, and this line stays open until "
                               "its prompt returns. Type the answer; leave it empty to just "
                               "press Enter."));
        break;
    case Pause::NoPrompt:
        m_question->setText(tr("No prompt recognised, so nothing has been sent. Get the device "
                               "to its prompt in the terminal, or change the prompt pattern."));
        break;
    default:
        break;
    }
    updateSendLabel();
    if (m_edit->isVisible()) {
        m_edit->setFocus();
        m_edit->selectAll();
    }
}

void ConfigPushBar::updateSendLabel()
{
    using Pause = ConfigPush::Pause;
    const int n = m_push->current();
    const QList<ConfigPush::Line> lines = m_push->lines();
    if (m_reason == Pause::NoPrompt)
        m_send->setText(tr("Try Again"));
    else if (m_reason == Pause::Question)
        m_send->setText(tr("Send Answer"));
    else if (n < lines.size() && m_edit->text() != lines[n].text)
        m_send->setText(tr("Send Amended"));
    else
        m_send->setText(tr("Send Again"));
}

void ConfigPushBar::sendEdited()
{
    const QString text = m_edit->text();
    const bool answer = m_reason == ConfigPush::Pause::Question;
    showRunning();
    if (answer)
        m_push->answer(text);
    else
        m_push->retry(text);
}

void ConfigPushBar::answerSecret(const QString &text)
{
    if (!waitingForAnswer())
        return;
    m_edit->clear();
    showRunning();
    m_push->answer(text);
}

void ConfigPushBar::onFinished(bool complete)
{
    m_reason = ConfigPush::Pause::None;
    emit inputBlockChanged(false);
    m_ask->hide();
    m_bar->hide();
    m_pause->hide();
    m_stop->hide();
    m_copy->show();
    m_close->show();
    const QString summary = m_push->report().section('\n', 0, 0);
    m_status->setText(complete ? tr("Finished: %1").arg(summary)
                               : tr("Stopped: %1").arg(summary));
    m_again->setVisible(m_push->failedCount() > 0);
    updateList();
    show();
}

void ConfigPushBar::updateList()
{
    if (!m_list->isVisible())
        return;
    const QList<ConfigPush::Line> lines = m_push->lines();
    m_list->clear();
    for (int i = 0; i < lines.size(); i++) {
        const ConfigPush::Line &l = lines[i];
        QString mark = QStringLiteral("  ");
        if (l.state == ConfigPush::LineState::Ok)
            mark = tr("ok");
        else if (l.state == ConfigPush::LineState::Failed)
            mark = tr("!!");
        else if (l.state == ConfigPush::LineState::Skipped)
            mark = tr("--");
        else if (i == m_push->current() && m_push->running())
            mark = QStringLiteral(">>");
        auto *item = new QListWidgetItem(QStringLiteral("%1 %2  %3").arg(mark)
                                             .arg(i + 1, 3).arg(l.text), m_list);
        item->setToolTip(l.error);
    }
    if (m_push->current() < m_list->count())
        m_list->scrollToItem(m_list->item(m_push->current()));
}
