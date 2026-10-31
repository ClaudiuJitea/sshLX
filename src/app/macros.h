// Macros - the user's saved command sequences, run from the terminal menu.
// One line is sent as it is; several go through Paste Configuration.
#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLineEdit;
class QListWidget;
class QPlainTextEdit;

struct Macro {
    QString name;
    QString text;
};

QList<Macro> loadMacros();
void saveMacros(const QList<Macro> &macros);

class MacroDialog : public QDialog {
    Q_OBJECT
public:
    explicit MacroDialog(QWidget *parent);

private:
    void select(int row);
    void store();

    QList<Macro> m_macros;
    int m_row = -1;
    QListWidget *m_list;
    QLineEdit *m_name;
    QPlainTextEdit *m_text;
};
