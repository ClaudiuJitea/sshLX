// PromptBar - shows PuTTY's host-key / weak-crypto / log-file questions
// inside the affected tab, never app-modal. It sits outside the terminal
// area (the server cannot draw there) and carries sshLX's trust marker, in
// the spirit of PuTTY's anti-spoofing trust sigil.
#pragma once

#include <QFrame>
#include <QList>

#include "sshlx_engine.h"

class QLabel;
class QPushButton;
class QLineEdit;
class QPlainTextEdit;
class QVBoxLayout;
class QHBoxLayout;

class PromptBar : public QFrame {
    Q_OBJECT
public:
    explicit PromptBar(QWidget *parent = nullptr);

    // Queue a request (copied). Shown immediately if nothing else is shown.
    void addRequest(const sshlx_prompt_request *req);
    void cancel(sshlx_prompt_id id);
    void clearAll();
    bool hasPending() const { return !m_queue.isEmpty(); }

signals:
    void answered(sshlx_prompt_id id, sshlx_answer answer);
    void passwordAnswered(sshlx_prompt_id id, const QString &password, bool provided);

private:
    struct Item { sshlx_dt_kind kind; QString text; };
    struct Request {
        sshlx_prompt_id id;
        sshlx_prompt_kind kind;
        QList<Item> items;
        QString accept, once, cancel, host, keytype, filename, promptText;
        int port = 0;
    };
    void showCurrent();
    void finish(sshlx_answer a);

    QList<Request> m_queue;
    QLabel *m_icon, *m_title, *m_body;
    QPlainTextEdit *m_display;
    QPushButton *m_moreInfo, *m_accept, *m_once, *m_cancel;
    QPlainTextEdit *m_moreText;
    QLineEdit *m_password;
};
