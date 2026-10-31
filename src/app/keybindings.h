// KeyBindings - keys that send text, or run a macro or script, while a
// terminal has the focus. A mapped key does not reach the session, and takes
// priority over application shortcuts.
#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

class QKeyEvent;
class QTableWidget;

struct KeyBinding {
    enum Kind { Text, Macro, Script };
    QKeySequence key;
    Kind kind = Text;
    QString value;     // the text, the macro's name, or the script's path
};

namespace KeyBindings {
QList<KeyBinding> load();
void save(const QList<KeyBinding> &list);
const KeyBinding *find(const QKeyEvent *e);
// \r \n \t \e \\ and \xHH become those bytes.
QString decodeText(const QString &text);
}

class KeyBindingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyBindingsDialog(QWidget *parent);
    void addRow(const KeyBinding &b);
    QList<KeyBinding> bindings() const;

private:
    void fillValues(int row);
    QTableWidget *m_table;
};
