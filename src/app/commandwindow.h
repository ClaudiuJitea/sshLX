// CommandWindow - types a command once and sends it to several tabs: the
// current one, those visible, all of them, or a chosen few. One line goes out
// straight away; several go through Paste Configuration's line-by-line
// sending, each tab pausing on its own if its device complains.
#pragma once

#include <QList>
#include <QPointer>
#include <QStringList>
#include <QWidget>

class DocumentArea;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class SessionWidget;

class CommandWindow : public QWidget {
    Q_OBJECT
public:
    enum Target { Current, Visible, All, Chosen };

    explicit CommandWindow(DocumentArea *docs, QWidget *parent = nullptr);

    void setTarget(Target t);
    void setChosen(const QList<SessionWidget *> &tabs);
    void setTargetGroup(const QString &name);
    // The list of tab groups changed.
    void refreshGroups();
    // Each key typed in a terminal also goes to the other target tabs.
    void setLive(bool on);
    bool live() const;
    // The tabs the target covers, whether or not they can take a command now.
    QList<SessionWidget *> targets() const;
    // Returns how many tabs took it; the label says what happened to the rest.
    int send(const QString &text);
    void focusInput();
    QPlainTextEdit *input() const { return m_input; }

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void sendInput();
    void updateSummary();
    void buildChooser();
    void history(int delta);

    DocumentArea *m_docs;
    QComboBox *m_target;
    QToolButton *m_choose;
    QPlainTextEdit *m_input;
    class QCheckBox *m_live;
    QPushButton *m_send;
    QLabel *m_summary;
    QList<QPointer<SessionWidget>> m_chosen;
    QStringList m_history;
    int m_historyPos = 0;
};
