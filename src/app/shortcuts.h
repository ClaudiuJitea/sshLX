// Application shortcut registry.
//
// Terminals need almost every key, so sshLX only takes the key sequences
// that are explicitly registered here (all user-configurable in Tools >
// Options). Everything else goes to the session. "Send next shortcut to
// terminal" arms a one-shot bypass so even a registered shortcut can be
// typed into the session.
#pragma once

#include <QAction>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QKeyEvent;

class Shortcuts : public QObject {
    Q_OBJECT
public:
    struct Entry {
        QString id;
        QString name;
        QKeySequence defaultSeq;
        QKeySequence seq;
        QPointer<QAction> action;
    };

    static Shortcuts &instance();

    // Register an action under a stable id; applies the saved/default key.
    void add(const QString &id, QAction *action, const QKeySequence &def);
    QList<Entry> entries() const { return m_entries; }
    void setSequence(const QString &id, const QKeySequence &seq);
    void resetAll();
    void load();
    void save() const;

    // Decide whether a key event should go to the terminal (true) or be
    // left for an application shortcut (false).
    bool terminalShouldTake(const QKeyEvent *e);
    void armSendNext();
    bool sendNextArmed() const { return m_sendNext; }

signals:
    void sendNextChanged(bool armed);

private:
    Shortcuts() = default;
    QList<Entry> m_entries;
    bool m_sendNext = false;
};
