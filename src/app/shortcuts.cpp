#include "shortcuts.h"

#include <QKeyEvent>
#include <QSettings>

Shortcuts &Shortcuts::instance()
{
    static Shortcuts s;
    return s;
}

void Shortcuts::add(const QString &id, QAction *action, const QKeySequence &def)
{
    Entry e;
    e.id = id;
    e.name = action->text().remove('&');
    e.defaultSeq = def;
    e.action = action;
    QSettings st;
    QVariant v = st.value("shortcuts/" + id);
    e.seq = v.isValid() ? QKeySequence(v.toString()) : def;
    action->setShortcut(e.seq);
    action->setShortcutContext(Qt::ApplicationShortcut);
    m_entries.append(e);
}

void Shortcuts::setSequence(const QString &id, const QKeySequence &seq)
{
    for (Entry &e : m_entries) {
        if (e.id == id) {
            e.seq = seq;
            if (e.action)
                e.action->setShortcut(seq);
        }
    }
}

void Shortcuts::resetAll()
{
    for (Entry &e : m_entries) {
        e.seq = e.defaultSeq;
        if (e.action)
            e.action->setShortcut(e.seq);
    }
}

void Shortcuts::load()
{
    QSettings st;
    for (Entry &e : m_entries) {
        QVariant v = st.value("shortcuts/" + e.id);
        if (v.isValid())
            setSequence(e.id, QKeySequence(v.toString()));
    }
}

void Shortcuts::save() const
{
    QSettings st;
    for (const Entry &e : m_entries)
        st.setValue("shortcuts/" + e.id, e.seq.toString());
}

void Shortcuts::armSendNext()
{
    m_sendNext = true;
    emit sendNextChanged(true);
}

bool Shortcuts::terminalShouldTake(const QKeyEvent *e)
{
    const int key = e->key();
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
        key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_unknown)
        return true;
    if (m_sendNext) {
        m_sendNext = false;
        emit sendNextChanged(false);
        return true;
    }
    QKeySequence pressed(e->keyCombination());
    bool registered = false;
    for (const Entry &en : m_entries) {
        if (!en.seq.isEmpty() &&
            en.seq.matches(pressed) == QKeySequence::ExactMatch) {
            registered = true;
            break;
        }
    }
    return !registered;
}
