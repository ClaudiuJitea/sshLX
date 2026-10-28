#include "promptbar.h"

#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

PromptBar::PromptBar(QWidget *parent) : QFrame(parent)
{
    setObjectName("PromptBar");
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, pal.color(QPalette::AlternateBase));
    setPalette(pal);

    auto *outer = new QHBoxLayout(this);
    m_icon = new QLabel;
    m_icon->setPixmap(QApplication::windowIcon().pixmap(32, 32));
    m_icon->setToolTip(tr("This question comes from sshLX itself, not from "
                          "the remote server."));
    m_icon->setAlignment(Qt::AlignTop);
    outer->addWidget(m_icon);

    auto *col = new QVBoxLayout;
    outer->addLayout(col, 1);
    m_title = new QLabel;
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);
    col->addWidget(m_title);
    m_body = new QLabel;
    m_body->setWordWrap(true);
    m_body->setTextFormat(Qt::RichText);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    col->addWidget(m_body);
    m_display = new QPlainTextEdit;
    m_display->setReadOnly(true);
    m_display->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_display->setMaximumHeight(60);
    col->addWidget(m_display);
    m_moreText = new QPlainTextEdit;
    m_moreText->setReadOnly(true);
    m_moreText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_moreText->setVisible(false);
    m_moreText->setMaximumHeight(140);
    col->addWidget(m_moreText);
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    col->addWidget(m_password);

    auto *buttons = new QHBoxLayout;
    m_moreInfo = new QPushButton(tr("More info..."));
    m_accept = new QPushButton;
    m_once = new QPushButton;
    m_cancel = new QPushButton;
    buttons->addWidget(m_moreInfo);
    buttons->addStretch(1);
    buttons->addWidget(m_accept);
    buttons->addWidget(m_once);
    buttons->addWidget(m_cancel);
    col->addLayout(buttons);

    connect(m_moreInfo, &QPushButton::clicked, this, [this] {
        m_moreText->setVisible(!m_moreText->isVisible());
    });
    connect(m_accept, &QPushButton::clicked, this, [this] {
        if (!m_queue.isEmpty() && m_queue.first().kind == SSHLX_PROMPT_LOG_APPEND)
            finish(SSHLX_ANSWER_OVERWRITE);
        else
            finish(SSHLX_ANSWER_ACCEPT);
    });
    connect(m_once, &QPushButton::clicked, this, [this] {
        if (!m_queue.isEmpty() && m_queue.first().kind == SSHLX_PROMPT_LOG_APPEND)
            finish(SSHLX_ANSWER_APPEND);
        else
            finish(SSHLX_ANSWER_ONCE);
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        finish(SSHLX_ANSWER_CANCEL);
    });
    connect(m_password, &QLineEdit::returnPressed, m_accept, &QPushButton::click);
    setVisible(false);
}

void PromptBar::addRequest(const sshlx_prompt_request *req)
{
    Request r;
    r.id = req->id;
    r.kind = req->kind;
    for (size_t i = 0; i < req->n_items; i++)
        r.items.append({req->items[i].kind, QString::fromUtf8(req->items[i].text)});
    r.accept = QString::fromUtf8(req->accept_label ? req->accept_label : "");
    r.once = QString::fromUtf8(req->once_label ? req->once_label : "");
    r.cancel = QString::fromUtf8(req->cancel_label ? req->cancel_label : "");
    r.host = QString::fromUtf8(req->host ? req->host : "");
    r.keytype = QString::fromUtf8(req->keytype ? req->keytype : "");
    r.filename = QString::fromUtf8(req->filename ? req->filename : "");
    r.promptText = QString::fromUtf8(req->prompt_text ? req->prompt_text : "");
    r.port = req->port;
    m_queue.append(r);
    if (m_queue.size() == 1)
        showCurrent();
}

void PromptBar::cancel(sshlx_prompt_id id)
{
    for (int i = 0; i < m_queue.size(); i++) {
        if (m_queue[i].id == id) {
            m_queue.removeAt(i);
            if (i == 0)
                showCurrent();
            return;
        }
    }
}

void PromptBar::clearAll()
{
    m_queue.clear();
    showCurrent();
}

void PromptBar::showCurrent()
{
    if (m_queue.isEmpty()) {
        setVisible(false);
        return;
    }
    const Request &r = m_queue.first();
    QString title, body, display, more;
    for (const Item &it : r.items) {
        switch (it.kind) {
        case SSHLX_DT_TITLE:
            title = it.text;
            break;
        case SSHLX_DT_SCARY_HEADING:
            body += QStringLiteral("<p><b><font color=\"#c00000\">%1</font></b></p>")
                        .arg(it.text.toHtmlEscaped());
            break;
        case SSHLX_DT_PARA:
            body += QStringLiteral("<p>%1</p>").arg(it.text.toHtmlEscaped());
            break;
        case SSHLX_DT_DISPLAY:
            display += it.text + "\n";
            break;
        case SSHLX_DT_MORE_INFO_KEY:
            more += it.text + ":\n";
            break;
        case SSHLX_DT_MORE_INFO_VALUE_SHORT:
        case SSHLX_DT_MORE_INFO_VALUE_BLOB:
            more += "  " + it.text + "\n";
            break;
        default:
            break;   /* PROMPT / BATCH_ABORT are console-only */
        }
    }
    if (r.kind == SSHLX_PROMPT_PASSWORD) {
        title = tr("Password from keyring");
        body = tr("<p>%1</p>").arg(r.promptText.toHtmlEscaped());
    }
    if (title.isEmpty())
        title = tr("sshLX");
    m_title->setText(title);
    m_body->setText(body);
    m_display->setPlainText(display.trimmed());
    m_display->setVisible(!display.isEmpty());
    m_moreText->setPlainText(more.trimmed());
    m_moreText->setVisible(false);
    m_moreInfo->setVisible(!more.isEmpty());
    m_password->setVisible(r.kind == SSHLX_PROMPT_PASSWORD);
    m_password->clear();
    m_accept->setText(r.accept);
    m_once->setText(r.once);
    m_once->setVisible(!r.once.isEmpty());
    m_cancel->setText(r.cancel);
    // As PuTTY: the safe choice (Cancel / No) is the default button.
    m_cancel->setDefault(r.kind != SSHLX_PROMPT_PASSWORD);
    m_accept->setDefault(r.kind == SSHLX_PROMPT_PASSWORD);
    for (QPushButton *b : {m_accept, m_once, m_cancel}) {
        b->setProperty("role", b->isDefault() ? "primary" : "secondary");
        b->style()->unpolish(b);
        b->style()->polish(b);
    }
    setVisible(true);
    if (r.kind == SSHLX_PROMPT_PASSWORD)
        m_password->setFocus();
}

void PromptBar::finish(sshlx_answer a)
{
    if (m_queue.isEmpty())
        return;
    Request r = m_queue.takeFirst();
    if (r.kind == SSHLX_PROMPT_PASSWORD) {
        QString pw = m_password->text();
        m_password->clear();
        emit passwordAnswered(r.id, pw, a == SSHLX_ANSWER_ACCEPT);
    } else {
        emit answered(r.id, a);
    }
    showCurrent();
}
