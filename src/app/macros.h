// Macros - the user's saved command sequences, run from the terminal menu.
// One line is sent as it is; several go through Paste Configuration.
#pragma once

#include <QDialog>
#include <QList>
#include <QObject>
#include <QString>
#include <QToolBar>

#include <functional>

class QComboBox;
class QLineEdit;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QPushButton;

struct Macro {
    QString name;
    QString text;
    QString folder;     // "Cisco/Show", or empty for the top level
    QString script;     // a Python script to run instead of text
};

QList<Macro> loadMacros();
void saveMacros(const QList<Macro> &macros);
// Tells open menus and the button bar that the list changed.
class MacroEvents : public QObject {
    Q_OBJECT
public:
    static MacroEvents *instance();
signals:
    void changed();
};
// Adds the macros to menu in submenus by folder; enabled greys them out.
void addMacrosToMenu(QMenu *menu, const QList<Macro> &macros, bool enabled,
                     const std::function<void(const Macro &)> &run);

// A row of buttons for the macros. Folders are drop-down menus.
class ButtonBar : public QToolBar {
    Q_OBJECT
public:
    explicit ButtonBar(QWidget *parent = nullptr);
    void rebuild();
signals:
    // onTargets: run on the Command Window's tabs rather than the current one.
    void macroTriggered(const Macro &macro, bool onTargets);
    void manageRequested();
};

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
    QComboBox *m_folder, *m_kind;
    QLineEdit *m_script;
    QPushButton *m_browse;
    void updateKind();
};
