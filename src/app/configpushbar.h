// ConfigPushBar - shows a running config push inside its tab, above the
// terminal so the device's output stays in view. Asks what to do when the
// push pauses; the terminal is usable meanwhile, to fix things by hand.
#pragma once

#include <QFrame>

#include "configpush.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QWidget;

class ConfigPushBar : public QFrame {
    Q_OBJECT
public:
    explicit ConfigPushBar(ConfigPush *push, QWidget *parent = nullptr);

    bool waitingForAnswer() const { return m_reason == ConfigPush::Pause::Question; }
    // Answers the paused question without showing or keeping the text.
    void answerSecret(const QString &text);

signals:
    // True while lines are going out and typing would land between them.
    void inputBlockChanged(bool blocked);
    void closed();
    // The Saved Password menu is opening; fill it.
    void passwordMenuRequested(QMenu *menu);

private:
    void showRunning();
    void onProgress(int current, int total);
    void onPaused(ConfigPush::Pause reason, const QString &output);
    void onFinished(bool complete);
    void sendEdited();
    void updateSendLabel();
    void updateList();

    ConfigPush *m_push;
    ConfigPush::Pause m_reason = ConfigPush::Pause::None;
    QLabel *m_status;
    QProgressBar *m_bar;
    QPushButton *m_pause, *m_stop, *m_copy, *m_close, *m_lines, *m_again;
    QListWidget *m_list;
    QWidget *m_ask;
    QLabel *m_question;
    QPlainTextEdit *m_output;
    QLineEdit *m_edit;
    QPushButton *m_send, *m_saved, *m_skip, *m_wait, *m_ignore;
};
