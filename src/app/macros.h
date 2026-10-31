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
    QString bar;        // the button bar it is on; empty: the default bar
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
    // Every bar name, the default one ("") first.
    static QStringList barNames();
    QString shownBar() const { return m_forced.isNull() ? m_chosen : m_forced; }
    // What the user picked from the list.
    void setChosenBar(const QString &name);
    // A connection's own bar for its tab; null: back to the user's pick.
    void setForcedBar(const QString &name);
signals:
    // onTargets: run on the Command Window's tabs rather than the current one.
    void macroTriggered(const Macro &macro, bool onTargets);
    void manageRequested();

private:
    QString m_chosen, m_forced;
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
    QComboBox *m_folder, *m_kind, *m_bar;
    QLineEdit *m_script;
    QPushButton *m_browse;
    void updateKind();
};
